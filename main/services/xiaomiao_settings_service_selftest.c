/*
 * Settings Service self test (goal node 9, checkpoint 2; schema v4 and
 * the v1/v2/v3 migrations, goal 20261001-1449).
 *
 * Covers the constructible recovery paths of the Service:
 *   step 1  key missing            -> defaults installed and persisted
 *           plus the argument checks, init() idempotency and a
 *           non-default write verified through the raw stored bytes
 *   step 2  valid v4 blob          -> loaded from NVS after a real
 *                                     restart (the non-default values
 *                                     written in step 1 come back)
 *   step 3  valid v1 blob          -> migrated in place: both original
 *                                     values survive, the new fields
 *                                     get their defaults and the entry
 *                                     is rewritten as v4
 *   step 4  valid v2 blob          -> migrated the same way, keeping
 *                                     its three booleans
 *   step 5  valid v3 blob          -> migrated the same way, keeping
 *                                     the five v3 fields
 *   step 6  unknown schema version -> defaults restored, this key repaired
 *   step 7  out-of-range minutes   -> defaults restored
 *   step 8  invalid idle minutes   -> defaults restored (not a menu
 *                                     candidate)
 *   step 9  blob shorter than v4   -> defaults restored
 *   step 10 blob longer than v4    -> NVS rejects the read, the Service
 *                                     degrades and keeps the defaults
 *                                     readable, then the namespace is
 *                                     restored to a clean state
 *
 * The blob length cases are deliberately split: a shorter blob reaches
 * the Service and fails its own length check (recovery rule 2, repair),
 * while a longer one is refused by nvs_get_blob() with
 * ESP_ERR_NVS_INVALID_LENGTH before the data arrives (rule 3, degrade,
 * nothing is overwritten).
 *
 * "NVS unavailable degradation" (an nvs_flash_init or nvs_open failure)
 * is not constructible without changing the partition table, so it
 * stays inspection-only and is recorded as unverified in the goal
 * document.
 *
 * The test never calls nvs_flash_erase(): it only manipulates the two
 * keys it owns, and it never modifies the Service source.
 */

#include "xiaomiao_settings_service_selftest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "xiaomiao_settings_service.h"

static const char TAG[] = "settings_selftest";

#define SELFTEST_NAMESPACE  "xiaomiao"
#define SELFTEST_KEY        "settings"
#define SELFTEST_STAGE_KEY  "selftest_stage"
#define SELFTEST_VERSION    4
#define SELFTEST_PAYLOAD    6
#define SELFTEST_STEP_COUNT 10

/*
 * Independent mirrors of the documented layouts. They are declared here
 * again on purpose instead of being shared with the Service, so the
 * test breaks if an on-Flash layout ever changes without a schema
 * version bump.
 */
typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t pomodoro_sound_enabled;
    uint8_t focus_minutes;
    uint8_t break_minutes;
    uint8_t screen_idle_minutes;
} selftest_blob_v4_t;

typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t pomodoro_sound_enabled;
    uint8_t focus_minutes;
    uint8_t break_minutes;
    uint8_t reserved;
} selftest_blob_v3_t;

typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t pomodoro_sound_enabled;
    uint8_t reserved;
} selftest_blob_v2_t;

typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t reserved[2];
} selftest_blob_v1_t;

_Static_assert(sizeof(selftest_blob_v4_t) == 10, "v4 mirror must stay 10 bytes");
_Static_assert(sizeof(selftest_blob_v3_t) == 10, "v3 mirror must stay 10 bytes");
_Static_assert(sizeof(selftest_blob_v2_t) == 8, "v2 mirror must stay 8 bytes");
_Static_assert(sizeof(selftest_blob_v1_t) == 8, "v1 mirror must stay 8 bytes");

/*
 * Halt instead of abort(): this build restarts itself between steps, so
 * aborting would reboot straight back into the failing step and hide
 * the message behind a reboot loop.
 */
#define SELFTEST_CHECK(cond, msg)                                             \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ESP_LOGE(TAG, "SETTINGS_SERVICE_SELF_TEST: FAIL %s (%s)", msg,    \
                     #cond);                                                  \
            vTaskSuspend(NULL);                                               \
        }                                                                     \
    } while (0)

static esp_err_t selftest_open(nvs_handle_t *out_handle)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        return err;
    }

    return nvs_open(SELFTEST_NAMESPACE, NVS_READWRITE, out_handle);
}

static esp_err_t selftest_write_blob(const void *data, size_t size)
{
    nvs_handle_t handle = 0;
    esp_err_t err = selftest_open(&handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, SELFTEST_KEY, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t selftest_read_raw(void *out_value, size_t *out_length)
{
    nvs_handle_t handle = 0;
    esp_err_t err = selftest_open(&handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, SELFTEST_KEY, out_value, out_length);
    nvs_close(handle);
    return err;
}

static esp_err_t selftest_erase_key(const char *key)
{
    nvs_handle_t handle = 0;
    esp_err_t err = selftest_open(&handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Erasing what is already gone is the state we want. */
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static uint32_t selftest_read_stage(void)
{
    nvs_handle_t handle = 0;
    if (selftest_open(&handle) != ESP_OK) {
        return 0;
    }

    uint32_t stage = 0;
    esp_err_t err = nvs_get_u32(handle, SELFTEST_STAGE_KEY, &stage);
    nvs_close(handle);

    return (err == ESP_OK) ? stage : 0;
}

static esp_err_t selftest_write_stage(uint32_t stage)
{
    nvs_handle_t handle = 0;
    esp_err_t err = selftest_open(&handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, SELFTEST_STAGE_KEY, stage);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

/* Assert that the step marker survived, which is the evidence that a
 * repair touched only `xiaomiao/settings`. */
static void selftest_expect_marker(uint32_t expected)
{
    SELFTEST_CHECK(selftest_read_stage() == expected,
                   "repair left the other namespace keys alone");
}

/* Read the stored blob and assert it is a valid, normalised v4 blob. */
static void selftest_expect_valid_blob(uint8_t expected_wifi,
                                       uint8_t expected_sound,
                                       uint8_t expected_pomodoro,
                                       uint8_t expected_focus,
                                       uint8_t expected_break,
                                       uint8_t expected_idle)
{
    selftest_blob_v4_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t length = sizeof(blob);

    esp_err_t err = selftest_read_raw(&blob, &length);
    SELFTEST_CHECK(err == ESP_OK, "read back stored blob");
    SELFTEST_CHECK(length == sizeof(blob), "stored blob has the v4 length");
    SELFTEST_CHECK(blob.schema_version == SELFTEST_VERSION, "stored version is 4");
    SELFTEST_CHECK(blob.payload_size == SELFTEST_PAYLOAD, "stored payload size is 6");
    SELFTEST_CHECK(blob.wifi_auto_connect == expected_wifi,
                   "stored wifi_auto_connect matches");
    SELFTEST_CHECK(blob.sound_enabled == expected_sound, "stored sound_enabled matches");
    SELFTEST_CHECK(blob.pomodoro_sound_enabled == expected_pomodoro,
                   "stored pomodoro_sound_enabled matches");
    SELFTEST_CHECK(blob.focus_minutes == expected_focus,
                   "stored focus_minutes matches");
    SELFTEST_CHECK(blob.break_minutes == expected_break,
                   "stored break_minutes matches");
    SELFTEST_CHECK(blob.screen_idle_minutes == expected_idle,
                   "stored screen_idle_minutes matches");
}

/* Assert the loaded snapshot and the reported source after init(). */
static void selftest_expect_loaded(xiaomiao_settings_source_t expected_source,
                                   bool expected_wifi, bool expected_sound,
                                   bool expected_pomodoro,
                                   uint8_t expected_focus, uint8_t expected_break,
                                   uint8_t expected_idle,
                                   const char *what)
{
    esp_err_t err = xiaomiao_settings_service_init();
    SELFTEST_CHECK(err == ESP_OK, what);
    SELFTEST_CHECK(xiaomiao_settings_source() == expected_source,
                   "reported source matches the loaded path");

    xiaomiao_settings_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    err = xiaomiao_settings_get(&snapshot);
    SELFTEST_CHECK(err == ESP_OK, "get after init");
    SELFTEST_CHECK(snapshot.wifi_auto_connect == expected_wifi,
                   "loaded wifi_auto_connect matches");
    SELFTEST_CHECK(snapshot.sound_enabled == expected_sound,
                   "loaded sound_enabled matches");
    SELFTEST_CHECK(snapshot.pomodoro_sound_enabled == expected_pomodoro,
                   "loaded pomodoro_sound_enabled matches");
    SELFTEST_CHECK(snapshot.focus_minutes == expected_focus,
                   "loaded focus_minutes matches");
    SELFTEST_CHECK(snapshot.break_minutes == expected_break,
                   "loaded break_minutes matches");
    SELFTEST_CHECK(snapshot.screen_idle_minutes == expected_idle,
                   "loaded screen_idle_minutes matches");
}

static void selftest_step_defaults(void)
{
    xiaomiao_settings_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    /* Before init(): a NULL pointer is rejected and no snapshot is
     * served yet. */
    SELFTEST_CHECK(xiaomiao_settings_get(NULL) == ESP_ERR_INVALID_ARG,
                   "get(NULL) rejected");
    SELFTEST_CHECK(xiaomiao_settings_get(&snapshot) == ESP_ERR_INVALID_STATE,
                   "get before init rejected");
    SELFTEST_CHECK(xiaomiao_settings_set(NULL) == ESP_ERR_INVALID_ARG,
                   "set(NULL) rejected");
    SELFTEST_CHECK(xiaomiao_settings_set(&snapshot) == ESP_ERR_INVALID_STATE,
                   "set before init rejected");

    /* Recovery rule 1 starts from "the key does not exist". */
    SELFTEST_CHECK(selftest_erase_key(SELFTEST_KEY) == ESP_OK,
                   "clear the stored settings key");

    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_DEFAULTS, true, true, true, 25, 5, 2,
                           "init on a missing key succeeds");
    SELFTEST_CHECK(xiaomiao_settings_last_error() == ESP_OK,
                   "a first install is not an error");

    /* Idempotent: the second call returns the same result and does not
     * rewrite Flash. */
    SELFTEST_CHECK(xiaomiao_settings_service_init() == ESP_OK, "init is idempotent");
    SELFTEST_CHECK(xiaomiao_settings_source() == XIAOMIAO_SETTINGS_SOURCE_DEFAULTS,
                   "a repeated init keeps the source");

    selftest_expect_valid_blob(1, 1, 1, 25, 5, 2);

    /* Non-default write; the persisted bytes are what the next boot has
     * to recover. */
    const xiaomiao_settings_t changed = {
        .wifi_auto_connect = false,
        .sound_enabled = false,
        .pomodoro_sound_enabled = false,
        .focus_minutes = 7,
        .break_minutes = 2,
        .screen_idle_minutes = 10,
    };
    SELFTEST_CHECK(xiaomiao_settings_set(&changed) == ESP_OK,
                   "store a non-default configuration");
    SELFTEST_CHECK(xiaomiao_settings_source() == XIAOMIAO_SETTINGS_SOURCE_NVS,
                   "a committed write reports the nvs source");
    SELFTEST_CHECK(xiaomiao_settings_last_error() == ESP_OK,
                   "a committed write clears the error");

    selftest_expect_valid_blob(0, 0, 0, 7, 2, 10);

    SELFTEST_CHECK(xiaomiao_settings_get(&snapshot) == ESP_OK, "get after write");
    SELFTEST_CHECK(!snapshot.wifi_auto_connect && !snapshot.sound_enabled &&
                   !snapshot.pomodoro_sound_enabled &&
                   snapshot.focus_minutes == 7 && snapshot.break_minutes == 2 &&
                   snapshot.screen_idle_minutes == 10,
                   "the snapshot follows the committed write");
}

static void selftest_step_load_from_nvs(void)
{
    /* A real restart happened between step 1 and this call, so a
     * non-default configuration coming back proves reboot recovery. */
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_NVS, false, false, false, 7, 2, 10,
                           "init on a valid v4 blob succeeds");
    SELFTEST_CHECK(xiaomiao_settings_last_error() == ESP_OK, "a valid load is clean");

    /* Inject a valid legacy v1 entry: the values differ from what is
     * stored now, so step 3 can prove the migration read this blob and
     * not the previous v4 one. */
    selftest_blob_v1_t v1;
    memset(&v1, 0, sizeof(v1));
    v1.schema_version = 1;
    v1.payload_size = 2;
    v1.wifi_auto_connect = 1;
    v1.sound_enabled = 1;
    SELFTEST_CHECK(selftest_write_blob(&v1, sizeof(v1)) == ESP_OK,
                   "inject a valid legacy v1 blob");
}

static void selftest_step_v1_migration(void)
{
    /* Both v1 values survive, the new fields start at their defaults
     * and the entry is reported as loaded (not repaired). */
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_NVS, true, true, true, 25, 5, 2,
                           "a valid v1 blob migrates in place");
    SELFTEST_CHECK(xiaomiao_settings_last_error() == ESP_OK,
                   "a migration is not an error");

    /* The migration rewrote the entry as a valid v4 blob. */
    selftest_expect_valid_blob(1, 1, 1, 25, 5, 2);
    selftest_expect_marker(2);

    selftest_blob_v2_t v2;
    memset(&v2, 0, sizeof(v2));
    v2.schema_version = 2;
    v2.payload_size = 3;
    v2.wifi_auto_connect = 0;
    v2.sound_enabled = 1;
    v2.pomodoro_sound_enabled = 0;
    SELFTEST_CHECK(selftest_write_blob(&v2, sizeof(v2)) == ESP_OK,
                   "inject a valid legacy v2 blob");
}

static void selftest_step_v2_migration(void)
{
    /* The three v2 booleans survive, the phase lengths and the idle
     * minutes get their defaults and the entry is reported as loaded. */
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_NVS, false, true, false, 25, 5, 2,
                           "a valid v2 blob migrates in place");
    SELFTEST_CHECK(xiaomiao_settings_last_error() == ESP_OK,
                   "a v2 migration is not an error");
    selftest_expect_valid_blob(0, 1, 0, 25, 5, 2);
    selftest_expect_marker(3);

    /* Inject a valid legacy v3 entry with non-default phase lengths, so
     * step 5 can prove the migration preserved them. */
    selftest_blob_v3_t v3;
    memset(&v3, 0, sizeof(v3));
    v3.schema_version = 3;
    v3.payload_size = 5;
    v3.wifi_auto_connect = 1;
    v3.sound_enabled = 0;
    v3.pomodoro_sound_enabled = 1;
    v3.focus_minutes = 90;
    v3.break_minutes = 10;
    SELFTEST_CHECK(selftest_write_blob(&v3, sizeof(v3)) == ESP_OK,
                   "inject a valid legacy v3 blob");
}

static void selftest_step_v3_migration(void)
{
    /* The five v3 fields survive, the idle minutes get their default
     * and the entry is reported as loaded. */
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_NVS, true, false, true, 90, 10, 2,
                           "a valid v3 blob migrates in place");
    SELFTEST_CHECK(xiaomiao_settings_last_error() == ESP_OK,
                   "a v3 migration is not an error");
    selftest_expect_valid_blob(1, 0, 1, 90, 10, 2);
    selftest_expect_marker(4);

    selftest_blob_v4_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.schema_version = 99;
    blob.payload_size = SELFTEST_PAYLOAD;
    blob.wifi_auto_connect = 1;
    blob.sound_enabled = 1;
    blob.pomodoro_sound_enabled = 1;
    blob.focus_minutes = 25;
    blob.break_minutes = 5;
    blob.screen_idle_minutes = 2;
    SELFTEST_CHECK(selftest_write_blob(&blob, sizeof(blob)) == ESP_OK,
                   "inject an unknown schema version");
}

static void selftest_step_unknown_version(void)
{
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_RECOVERED, true, true, true, 25, 5, 2,
                           "an unknown version falls back to the defaults");
    /* The repair rewrote this key as a valid v4 blob... */
    selftest_expect_valid_blob(1, 1, 1, 25, 5, 2);
    /* ...and left every other key of the namespace alone. */
    selftest_expect_marker(5);

    selftest_blob_v4_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.schema_version = SELFTEST_VERSION;
    blob.payload_size = SELFTEST_PAYLOAD;
    blob.wifi_auto_connect = 1;
    blob.sound_enabled = 1;
    blob.pomodoro_sound_enabled = 1;
    blob.focus_minutes = 0;
    blob.break_minutes = 5;
    blob.screen_idle_minutes = 2;
    SELFTEST_CHECK(selftest_write_blob(&blob, sizeof(blob)) == ESP_OK,
                   "inject an out-of-range focus length");
}

static void selftest_step_bad_range(void)
{
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_RECOVERED, true, true, true, 25, 5, 2,
                           "an out-of-range length falls back to the defaults");
    selftest_expect_valid_blob(1, 1, 1, 25, 5, 2);
    selftest_expect_marker(6);

    /* Idle minutes must be a menu candidate (0/1/2/5/10); anything else
     * is damage, not a legal "custom" value. */
    selftest_blob_v4_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.schema_version = SELFTEST_VERSION;
    blob.payload_size = SELFTEST_PAYLOAD;
    blob.wifi_auto_connect = 1;
    blob.sound_enabled = 1;
    blob.pomodoro_sound_enabled = 1;
    blob.focus_minutes = 25;
    blob.break_minutes = 5;
    blob.screen_idle_minutes = 3;
    SELFTEST_CHECK(selftest_write_blob(&blob, sizeof(blob)) == ESP_OK,
                   "inject an invalid idle value");
}

static void selftest_step_bad_idle(void)
{
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_RECOVERED, true, true, true, 25, 5, 2,
                           "an invalid idle value falls back to the defaults");
    selftest_expect_valid_blob(1, 1, 1, 25, 5, 2);
    selftest_expect_marker(7);

    /* Four bytes: shorter than v4, so nvs_get_blob() succeeds and the
     * Service's own length check has to reject it. */
    const uint8_t short_blob[4] = { 4, 0, 6, 0 };
    SELFTEST_CHECK(selftest_write_blob(short_blob, sizeof(short_blob)) == ESP_OK,
                   "inject a blob shorter than v4");
}

static void selftest_step_short_blob(void)
{
    selftest_expect_loaded(XIAOMIAO_SETTINGS_SOURCE_RECOVERED, true, true, true, 25, 5, 2,
                           "a short blob falls back to the defaults");
    selftest_expect_valid_blob(1, 1, 1, 25, 5, 2);
    selftest_expect_marker(8);

    /* Sixteen bytes: longer than the v4 buffer, so nvs_get_blob()
     * refuses the read before the Service can see the data. */
    uint8_t long_blob[16];
    memset(long_blob, 0, sizeof(long_blob));
    long_blob[0] = SELFTEST_VERSION;
    long_blob[2] = SELFTEST_PAYLOAD;
    long_blob[4] = 1;
    long_blob[5] = 1;
    long_blob[6] = 1;
    long_blob[7] = 25;
    long_blob[8] = 5;
    long_blob[9] = 2;
    SELFTEST_CHECK(selftest_write_blob(long_blob, sizeof(long_blob)) == ESP_OK,
                   "inject a blob longer than v4");
}

static void selftest_step_long_blob(void)
{
    esp_err_t err = xiaomiao_settings_service_init();
    SELFTEST_CHECK(err == ESP_ERR_NVS_INVALID_LENGTH,
                   "a long blob is refused by nvs_get_blob");
    SELFTEST_CHECK(xiaomiao_settings_source() == XIAOMIAO_SETTINGS_SOURCE_DEGRADED,
                   "a failed read degrades the service");
    SELFTEST_CHECK(xiaomiao_settings_last_error() == ESP_ERR_NVS_INVALID_LENGTH,
                   "the raw read error is reported");
    SELFTEST_CHECK(xiaomiao_settings_service_init() == ESP_ERR_NVS_INVALID_LENGTH,
                   "init stays idempotent after a failure");

    /* The degraded state still serves a complete, readable snapshot. */
    xiaomiao_settings_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    SELFTEST_CHECK(xiaomiao_settings_get(&snapshot) == ESP_OK,
                   "get works in the degraded state");
    SELFTEST_CHECK(snapshot.wifi_auto_connect && snapshot.sound_enabled &&
                   snapshot.pomodoro_sound_enabled &&
                   snapshot.focus_minutes == 25 && snapshot.break_minutes == 5 &&
                   snapshot.screen_idle_minutes == 2,
                   "the degraded state serves the defaults");
    /* The unreadable entry is reported, not repaired: it must still be
     * the 16-byte blob this step injected, and the other key must have
     * survived too. */
    selftest_expect_marker(9);

    uint8_t probe[16];
    memset(probe, 0, sizeof(probe));
    size_t probe_length = sizeof(probe);
    SELFTEST_CHECK(selftest_read_raw(probe, &probe_length) == ESP_OK,
                   "read back the unreadable entry");
    SELFTEST_CHECK(probe_length == sizeof(probe),
                   "a degraded load does not overwrite the stored entry");

    /* Restore a clean namespace: erasing the key returns the next boot
     * to recovery rule 1, and no partition-level erase is involved. */
    SELFTEST_CHECK(selftest_erase_key(SELFTEST_KEY) == ESP_OK,
                   "restore the settings key");
    SELFTEST_CHECK(selftest_erase_key(SELFTEST_STAGE_KEY) == ESP_OK,
                   "clear the step marker");
}

void xiaomiao_settings_service_selftest_run(void)
{
    const uint32_t stage = selftest_read_stage();

    ESP_LOGI(TAG, "Settings service self test, step %u/%d",
             (unsigned)(stage + 1), SELFTEST_STEP_COUNT);

    switch (stage) {
    case 0:
        selftest_step_defaults();
        break;
    case 1:
        selftest_step_load_from_nvs();
        break;
    case 2:
        selftest_step_v1_migration();
        break;
    case 3:
        selftest_step_v2_migration();
        break;
    case 4:
        selftest_step_v3_migration();
        break;
    case 5:
        selftest_step_unknown_version();
        break;
    case 6:
        selftest_step_bad_range();
        break;
    case 7:
        selftest_step_bad_idle();
        break;
    case 8:
        selftest_step_short_blob();
        break;
    case 9:
        selftest_step_long_blob();
        break;
    default:
        ESP_LOGE(TAG, "SETTINGS_SERVICE_SELF_TEST: FAIL unknown step %u, erase key '%s' to restart",
                 (unsigned)stage, SELFTEST_STAGE_KEY);
        vTaskSuspend(NULL);
        break;
    }

    if (stage + 1 < SELFTEST_STEP_COUNT) {
        SELFTEST_CHECK(selftest_write_stage(stage + 1) == ESP_OK,
                       "advance the step marker");
        ESP_LOGI(TAG, "step %u/%d done, restarting", (unsigned)(stage + 1),
                 SELFTEST_STEP_COUNT);
        esp_restart();
    }

    ESP_LOGI(TAG, "SETTINGS_SERVICE_SELF_TEST: PASS");
    vTaskSuspend(NULL);
}
