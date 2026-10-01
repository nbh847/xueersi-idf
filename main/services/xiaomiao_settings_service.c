/*
 * Settings Service implementation (goal node 9).
 *
 * One namespace, one key: `xiaomiao/settings`. The stored value is a
 * fixed-width versioned blob, never the raw memory image of the public
 * xiaomiao_settings_t, so the on-Flash layout cannot drift with the
 * compiler's padding or with a future field addition (goal node 9,
 * "NVS schema and recovery rules").
 *
 * Recovery order, as fixed by the goal:
 *   1. key missing            -> defaults, then persist them
 *   2. blob invalid           -> defaults, rewrite this key only
 *   3. NVS unusable           -> memory defaults, degraded state
 *   4. partition-level errors -> recorded as they are, never erased
 *
 * The Service keeps a single in-memory snapshot and a single status.
 * Reads are served from memory, writes open the namespace, commit, and
 * only then replace the snapshot.
 */

#include "xiaomiao_settings_service.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char TAG[] = "settings_svc";

/* Fixed identity of the stored entry (goal node 9, "fixed identity"). */
#define SETTINGS_NVS_NAMESPACE "xiaomiao"
#define SETTINGS_NVS_KEY       "settings"
/* v2 added pomodoro_sound_enabled, v3 added the Pomodoro phase lengths
 * (goal 20261001-1036), v4 adds the UI idle minutes (goal
 * 20261001-1449). A stored v1, v2 or v3 entry is migrated in place:
 * every old value is preserved and the new fields get their defaults. */
#define SETTINGS_BLOB_VERSION  4
/* The payload behind the 4-byte header: three booleans and three minute
 * counts. */
#define SETTINGS_BLOB_PAYLOAD  6

/* Accepted ranges of the v3 minute fields; anything outside them means
 * the stored entry is damaged and the defaults are restored. */
#define SETTINGS_FOCUS_MIN_MINUTES 1
#define SETTINGS_FOCUS_MAX_MINUTES 180
#define SETTINGS_BREAK_MIN_MINUTES 1
#define SETTINGS_BREAK_MAX_MINUTES 60
#define SETTINGS_DEFAULT_FOCUS_MINUTES 25
#define SETTINGS_DEFAULT_BREAK_MINUTES 5

/* v4 idle minutes: 0 disables the idle screen, the rest are the menu
 * candidates (goal 20261001-1449, "Configuration and migration"). */
#define SETTINGS_DEFAULT_IDLE_MINUTES 2

static bool settings_idle_minutes_valid(uint8_t minutes)
{
    return minutes == 0 || minutes == 1 || minutes == 2 ||
           minutes == 5 || minutes == 10;
}

/*
 * Private on-Flash layouts. Fixed-width integers plus an explicit
 * reserved area keep the blobs byte-identical across builds. Reserved
 * bytes are written as zero and carry no business meaning, so they are
 * not validated on read.
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
} settings_blob_v4_t;

/* The previous layouts, read only for the older -> v4 migrations. */
typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t pomodoro_sound_enabled;
    uint8_t focus_minutes;
    uint8_t break_minutes;
    uint8_t reserved;
} settings_blob_v3_t;

typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t pomodoro_sound_enabled;
    uint8_t reserved;
} settings_blob_v2_t;

typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t reserved[2];
} settings_blob_v1_t;

/* The decoder reads fixed offsets, so a padding change must not slip
 * through unnoticed. */
_Static_assert(sizeof(settings_blob_v4_t) == 10,
               "settings blob v4 must stay 10 bytes");
_Static_assert(sizeof(settings_blob_v3_t) == 10,
               "settings blob v3 must stay 10 bytes");
_Static_assert(sizeof(settings_blob_v2_t) == 8,
               "settings blob v2 must stay 8 bytes");
_Static_assert(sizeof(settings_blob_v1_t) == 8,
               "settings blob v1 must stay 8 bytes");

static xiaomiao_settings_t s_settings;
static xiaomiao_settings_source_t s_source = XIAOMIAO_SETTINGS_SOURCE_DEFAULTS;
static esp_err_t s_last_error = ESP_OK;
static esp_err_t s_init_result = ESP_OK;
static bool s_initialized;
/* False once NVS could not be brought up. Writes then fail with the
 * recorded error instead of touching a handle that does not exist. */
static bool s_nvs_usable;

static void settings_defaults(xiaomiao_settings_t *settings)
{
    settings->wifi_auto_connect = true;
    settings->sound_enabled = true;
    settings->pomodoro_sound_enabled = true;
    settings->focus_minutes = SETTINGS_DEFAULT_FOCUS_MINUTES;
    settings->break_minutes = SETTINGS_DEFAULT_BREAK_MINUTES;
    settings->screen_idle_minutes = SETTINGS_DEFAULT_IDLE_MINUTES;
}

static const char *settings_source_name(xiaomiao_settings_source_t source)
{
    switch (source) {
    case XIAOMIAO_SETTINGS_SOURCE_NVS:
        return "nvs";
    case XIAOMIAO_SETTINGS_SOURCE_RECOVERED:
        return "recovered";
    case XIAOMIAO_SETTINGS_SOURCE_DEGRADED:
        return "degraded";
    case XIAOMIAO_SETTINGS_SOURCE_DEFAULTS:
    default:
        return "defaults";
    }
}

static void settings_blob_encode(const xiaomiao_settings_t *settings,
                                 settings_blob_v4_t *blob)
{
    /* Zero first, so a future reserved byte never carries stale stack
     * data into Flash. */
    memset(blob, 0, sizeof(*blob));
    blob->schema_version = SETTINGS_BLOB_VERSION;
    blob->payload_size = SETTINGS_BLOB_PAYLOAD;
    blob->wifi_auto_connect = settings->wifi_auto_connect ? 1 : 0;
    blob->sound_enabled = settings->sound_enabled ? 1 : 0;
    blob->pomodoro_sound_enabled = settings->pomodoro_sound_enabled ? 1 : 0;
    blob->focus_minutes = settings->focus_minutes;
    blob->break_minutes = settings->break_minutes;
    blob->screen_idle_minutes = settings->screen_idle_minutes;
}

/* Current layout. */
static void settings_blob_decode_v4(const settings_blob_v4_t *blob,
                                    xiaomiao_settings_t *settings)
{
    settings->wifi_auto_connect = (blob->wifi_auto_connect != 0);
    settings->sound_enabled = (blob->sound_enabled != 0);
    settings->pomodoro_sound_enabled = (blob->pomodoro_sound_enabled != 0);
    settings->focus_minutes = blob->focus_minutes;
    settings->break_minutes = blob->break_minutes;
    settings->screen_idle_minutes = blob->screen_idle_minutes;
}

/* The v3 layout has no idle minutes; the new field starts at its
 * default. */
static void settings_blob_decode_v3(const settings_blob_v3_t *blob,
                                    xiaomiao_settings_t *settings)
{
    settings->wifi_auto_connect = (blob->wifi_auto_connect != 0);
    settings->sound_enabled = (blob->sound_enabled != 0);
    settings->pomodoro_sound_enabled = (blob->pomodoro_sound_enabled != 0);
    settings->focus_minutes = blob->focus_minutes;
    settings->break_minutes = blob->break_minutes;
    settings->screen_idle_minutes = SETTINGS_DEFAULT_IDLE_MINUTES;
}

/* The v2 layout has no phase lengths; the new fields start at their
 * defaults. */
static void settings_blob_decode_v2(const settings_blob_v2_t *blob,
                                    xiaomiao_settings_t *settings)
{
    settings->wifi_auto_connect = (blob->wifi_auto_connect != 0);
    settings->sound_enabled = (blob->sound_enabled != 0);
    settings->pomodoro_sound_enabled = (blob->pomodoro_sound_enabled != 0);
    settings->focus_minutes = SETTINGS_DEFAULT_FOCUS_MINUTES;
    settings->break_minutes = SETTINGS_DEFAULT_BREAK_MINUTES;
    settings->screen_idle_minutes = SETTINGS_DEFAULT_IDLE_MINUTES;
}

/* The v1 layout has no pomodoro fields at all. */
static void settings_blob_decode_v1(const settings_blob_v1_t *blob,
                                    xiaomiao_settings_t *settings)
{
    settings->wifi_auto_connect = (blob->wifi_auto_connect != 0);
    settings->sound_enabled = (blob->sound_enabled != 0);
    settings->pomodoro_sound_enabled = true;
    settings->focus_minutes = SETTINGS_DEFAULT_FOCUS_MINUTES;
    settings->break_minutes = SETTINGS_DEFAULT_BREAK_MINUTES;
    settings->screen_idle_minutes = SETTINGS_DEFAULT_IDLE_MINUTES;
}

/*
 * A blob is usable only when its length, version, payload size and
 * field ranges all match its declared version. nvs_get_blob() reports
 * a stored entry longer than the buffer as ESP_ERR_NVS_INVALID_LENGTH
 * but returns a shorter one with the real length, so `length` is
 * checked rather than assumed.
 */
static bool settings_blob_v4_is_valid(const settings_blob_v4_t *blob, size_t length)
{
    if (length != sizeof(*blob)) {
        return false;
    }
    if (blob->schema_version != SETTINGS_BLOB_VERSION) {
        return false;
    }
    if (blob->payload_size != SETTINGS_BLOB_PAYLOAD) {
        return false;
    }
    if (blob->wifi_auto_connect > 1 || blob->sound_enabled > 1 ||
        blob->pomodoro_sound_enabled > 1) {
        return false;
    }
    if (blob->focus_minutes < SETTINGS_FOCUS_MIN_MINUTES ||
        blob->focus_minutes > SETTINGS_FOCUS_MAX_MINUTES) {
        return false;
    }
    if (blob->break_minutes < SETTINGS_BREAK_MIN_MINUTES ||
        blob->break_minutes > SETTINGS_BREAK_MAX_MINUTES) {
        return false;
    }
    if (!settings_idle_minutes_valid(blob->screen_idle_minutes)) {
        return false;
    }

    return true;
}

/* A stored v3 entry keeps its five fields; only the idle minutes are
 * missing and they start at the default. */
static bool settings_blob_v3_is_migratable(const settings_blob_v3_t *blob, size_t length)
{
    if (length != sizeof(*blob)) {
        return false;
    }
    if (blob->schema_version != 3 || blob->payload_size != 5) {
        return false;
    }
    if (blob->wifi_auto_connect > 1 || blob->sound_enabled > 1 ||
        blob->pomodoro_sound_enabled > 1) {
        return false;
    }
    if (blob->focus_minutes < SETTINGS_FOCUS_MIN_MINUTES ||
        blob->focus_minutes > SETTINGS_FOCUS_MAX_MINUTES) {
        return false;
    }

    return blob->break_minutes >= SETTINGS_BREAK_MIN_MINUTES &&
           blob->break_minutes <= SETTINGS_BREAK_MAX_MINUTES;
}

static bool settings_blob_v2_is_migratable(const settings_blob_v2_t *blob, size_t length)
{
    if (length != sizeof(*blob)) {
        return false;
    }
    if (blob->schema_version != 2 || blob->payload_size != 3) {
        return false;
    }

    return blob->wifi_auto_connect <= 1 && blob->sound_enabled <= 1 &&
           blob->pomodoro_sound_enabled <= 1;
}

static bool settings_blob_v1_is_migratable(const settings_blob_v1_t *blob, size_t length)
{
    if (length != sizeof(*blob)) {
        return false;
    }
    if (blob->schema_version != 1 || blob->payload_size != 2) {
        return false;
    }

    return blob->wifi_auto_connect <= 1 && blob->sound_enabled <= 1;
}

/*
 * Write and commit one configuration. Only `xiaomiao/settings` is
 * touched, so neither the other keys of this namespace nor the rest of
 * the partition, which later Services will own, are affected (goal
 * node 9, recovery rule 5).
 */
static esp_err_t settings_store(nvs_handle_t handle,
                                const xiaomiao_settings_t *settings)
{
    settings_blob_v4_t blob;
    settings_blob_encode(settings, &blob);

    esp_err_t err = nvs_set_blob(handle, SETTINGS_NVS_KEY, &blob, sizeof(blob));
    if (err != ESP_OK) {
        return err;
    }

    return nvs_commit(handle);
}

/*
 * Install the defaults in memory and try to persist them. A failed
 * write does not block anything, but it must not be reported as a
 * successful repair either: the source drops to DEGRADED instead of
 * keeping `on_success` (goal node 9, failure-path clause for a failed
 * default write-back).
 */
static esp_err_t settings_install_defaults(nvs_handle_t handle,
                                           xiaomiao_settings_source_t on_success)
{
    settings_defaults(&s_settings);

    esp_err_t err = settings_store(handle, &s_settings);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "storing default settings failed: %s (0x%x), continuing in memory",
                 esp_err_to_name(err), (unsigned)err);
        s_source = XIAOMIAO_SETTINGS_SOURCE_DEGRADED;
        s_last_error = err;
        return err;
    }

    s_source = on_success;
    return ESP_OK;
}

/*
 * Commit the merged in-memory snapshot after a migration decode. A
 * failed write keeps the merged values in memory and marks the Service
 * degraded; nothing is erased (goal 20261001-1449, "Configuration and
 * migration").
 */
static esp_err_t settings_store_merged(nvs_handle_t handle)
{
    const esp_err_t err = settings_store(handle, &s_settings);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "storing migrated settings failed: %s (0x%x), continuing in memory",
                 esp_err_to_name(err), (unsigned)err);
        s_source = XIAOMIAO_SETTINGS_SOURCE_DEGRADED;
        s_last_error = err;
        return err;
    }

    s_source = XIAOMIAO_SETTINGS_SOURCE_NVS;
    s_last_error = ESP_OK;
    return ESP_OK;
}

/*
 * Load the snapshot from NVS, migrating a stored v1/v2/v3 entry and
 * repairing a missing or damaged one. Every failure path leaves a
 * complete, readable in-memory configuration behind; none of them
 * aborts the boot.
 */
static esp_err_t settings_load(nvs_handle_t handle)
{
    /* All four layouts fit in the v4 buffer, so one read buffer serves
     * every migration; the union keeps the aliasing explicit. */
    union {
        settings_blob_v4_t v4;
        settings_blob_v3_t v3;
        settings_blob_v2_t v2;
        settings_blob_v1_t v1;
    } blob;
    memset(&blob, 0, sizeof(blob));
    size_t length = sizeof(blob);

    esp_err_t err = nvs_get_blob(handle, SETTINGS_NVS_KEY, &blob, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First boot. This is the normal install path, not a fault. */
        return settings_install_defaults(handle,
                                         XIAOMIAO_SETTINGS_SOURCE_DEFAULTS);
    }

    if (err != ESP_OK) {
        /* The read failed, so this boot cannot trust NVS either:
         * degrade instead of pretending the defaults were stored. */
        ESP_LOGW(TAG, "reading settings failed: %s (0x%x), using memory defaults",
                 esp_err_to_name(err), (unsigned)err);
        s_source = XIAOMIAO_SETTINGS_SOURCE_DEGRADED;
        s_last_error = err;
        return err;
    }

    if (settings_blob_v4_is_valid(&blob.v4, length)) {
        settings_blob_decode_v4(&blob.v4, &s_settings);
        s_source = XIAOMIAO_SETTINGS_SOURCE_NVS;
        s_last_error = ESP_OK;
        return ESP_OK;
    }

    if (settings_blob_v3_is_migratable(&blob.v3, length)) {
        /* Schema upgrade from v3: keep every stored value, give the
         * idle minutes their default and persist the result as v4 so
         * the migration runs only once. */
        settings_blob_decode_v3(&blob.v3, &s_settings);
        ESP_LOGI(TAG, "migrating stored settings v3 -> v4 (idle_minutes=%u)",
                 (unsigned)s_settings.screen_idle_minutes);
        return settings_store_merged(handle);
    }

    if (settings_blob_v2_is_migratable(&blob.v2, length)) {
        /* Schema upgrade from v2: keep every stored value, give the
         * new fields their defaults and persist the result as v4 so
         * the migration runs only once. */
        settings_blob_decode_v2(&blob.v2, &s_settings);
        ESP_LOGI(TAG, "migrating stored settings v2 -> v4 (focus_minutes=%u, break_minutes=%u)",
                 (unsigned)s_settings.focus_minutes,
                 (unsigned)s_settings.break_minutes);
        return settings_store_merged(handle);
    }

    if (settings_blob_v1_is_migratable(&blob.v1, length)) {
        /* Schema upgrade from v1: keep both original values, give the
         * new preferences their defaults and persist the result as v4
         * so the migration runs only once. */
        settings_blob_decode_v1(&blob.v1, &s_settings);
        ESP_LOGI(TAG, "migrating stored settings v1 -> v4 (pomodoro_sound_enabled=%d)",
                 (int)s_settings.pomodoro_sound_enabled);
        return settings_store_merged(handle);
    }

    /* Length, version or field range is wrong. The entry belongs to
     * this Service, so only this key is rewritten. */
    ESP_LOGW(TAG, "stored settings invalid (length=%u), restoring defaults",
             (unsigned)length);
    return settings_install_defaults(handle,
                                     XIAOMIAO_SETTINGS_SOURCE_RECOVERED);
}

esp_err_t xiaomiao_settings_service_init(void)
{
    if (s_initialized) {
        return s_init_result;
    }

    /* Marked first, so a failure below is still idempotent: the next
     * call returns the same result and never retries the writes. */
    s_initialized = true;
    settings_defaults(&s_settings);
    s_source = XIAOMIAO_SETTINGS_SOURCE_DEFAULTS;
    s_last_error = ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* ESP_ERR_NVS_NO_FREE_PAGES and ESP_ERR_NVS_NEW_VERSION_FOUND
         * are recorded as they are. Erasing the partition here would
         * also destroy the data later Services own (goal node 9,
         * recovery rule 4). */
        ESP_LOGW(TAG, "nvs_flash_init failed: %s (0x%x), using memory defaults",
                 esp_err_to_name(err), (unsigned)err);
        s_source = XIAOMIAO_SETTINGS_SOURCE_DEGRADED;
        s_last_error = err;
        s_init_result = err;
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open '%s' failed: %s (0x%x), using memory defaults",
                 SETTINGS_NVS_NAMESPACE, esp_err_to_name(err), (unsigned)err);
        s_source = XIAOMIAO_SETTINGS_SOURCE_DEGRADED;
        s_last_error = err;
        s_init_result = err;
        return err;
    }

    s_nvs_usable = true;
    err = settings_load(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings service degraded, source=%s",
                 settings_source_name(s_source));
    }
    else {
        ESP_LOGI(TAG, "settings service ready, source=%s (wifi_auto_connect=%d, sound_enabled=%d, pomodoro_sound_enabled=%d, focus_minutes=%u, break_minutes=%u, screen_idle_minutes=%u)",
                 settings_source_name(s_source), (int)s_settings.wifi_auto_connect,
                 (int)s_settings.sound_enabled,
                 (int)s_settings.pomodoro_sound_enabled,
                 (unsigned)s_settings.focus_minutes,
                 (unsigned)s_settings.break_minutes,
                 (unsigned)s_settings.screen_idle_minutes);
    }

    s_init_result = err;
    return err;
}

esp_err_t xiaomiao_settings_get(xiaomiao_settings_t *out_settings)
{
    if (out_settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        /* No snapshot exists yet: the boot chain initializes the
         * Service before any App can ask for a setting. */
        return ESP_ERR_INVALID_STATE;
    }

    *out_settings = s_settings;
    return ESP_OK;
}

esp_err_t xiaomiao_settings_set(const xiaomiao_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_nvs_usable) {
        /* Init already failed; report its raw error rather than opening
         * a namespace that was never brought up. */
        return (s_last_error != ESP_OK) ? s_last_error : ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open '%s' for write failed: %s (0x%x)",
                 SETTINGS_NVS_NAMESPACE, esp_err_to_name(err), (unsigned)err);
        s_source = XIAOMIAO_SETTINGS_SOURCE_DEGRADED;
        s_last_error = err;
        return err;
    }

    err = settings_store(handle, settings);
    nvs_close(handle);

    if (err != ESP_OK) {
        /* Flash and the snapshot both keep their previous value; the
         * caller gets the raw error and no success is reported. */
        ESP_LOGE(TAG, "storing settings failed: %s (0x%x), snapshot unchanged",
                 esp_err_to_name(err), (unsigned)err);
        s_source = XIAOMIAO_SETTINGS_SOURCE_DEGRADED;
        s_last_error = err;
        return err;
    }

    s_settings = *settings;
    s_source = XIAOMIAO_SETTINGS_SOURCE_NVS;
    s_last_error = ESP_OK;
    ESP_LOGI(TAG, "settings stored (wifi_auto_connect=%d, sound_enabled=%d, pomodoro_sound_enabled=%d, focus_minutes=%u, break_minutes=%u, screen_idle_minutes=%u)",
             (int)s_settings.wifi_auto_connect, (int)s_settings.sound_enabled,
             (int)s_settings.pomodoro_sound_enabled,
             (unsigned)s_settings.focus_minutes, (unsigned)s_settings.break_minutes,
             (unsigned)s_settings.screen_idle_minutes);
    return ESP_OK;
}

xiaomiao_settings_source_t xiaomiao_settings_source(void)
{
    return s_source;
}

esp_err_t xiaomiao_settings_last_error(void)
{
    return s_last_error;
}
