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
#define SETTINGS_BLOB_VERSION  1
/* The payload behind the 4-byte header: the two boolean fields. */
#define SETTINGS_BLOB_PAYLOAD  2

/*
 * Private on-Flash layout. Fixed-width integers plus an explicit
 * reserved area keep the blob byte-identical across builds. `reserved`
 * is written as zero and carries no business meaning, so it is not
 * validated on read.
 */
typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t wifi_auto_connect;
    uint8_t sound_enabled;
    uint8_t reserved[2];
} settings_blob_v1_t;

/* The decoder reads fixed offsets, so a padding change must not slip
 * through unnoticed. */
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
                                 settings_blob_v1_t *blob)
{
    /* Zero first, so the reserved bytes never carry stale stack data
     * into Flash. */
    memset(blob, 0, sizeof(*blob));
    blob->schema_version = SETTINGS_BLOB_VERSION;
    blob->payload_size = SETTINGS_BLOB_PAYLOAD;
    blob->wifi_auto_connect = settings->wifi_auto_connect ? 1 : 0;
    blob->sound_enabled = settings->sound_enabled ? 1 : 0;
}

static void settings_blob_decode(const settings_blob_v1_t *blob,
                                 xiaomiao_settings_t *settings)
{
    settings->wifi_auto_connect = (blob->wifi_auto_connect != 0);
    settings->sound_enabled = (blob->sound_enabled != 0);
}

/*
 * A blob is usable only when its length, version, payload size and
 * field ranges all match v1. nvs_get_blob() reports a stored entry
 * longer than the buffer as ESP_ERR_NVS_INVALID_LENGTH but returns a
 * shorter one with the real length, so `length` is checked rather than
 * assumed.
 */
static bool settings_blob_is_valid(const settings_blob_v1_t *blob, size_t length)
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
    if (blob->wifi_auto_connect > 1 || blob->sound_enabled > 1) {
        return false;
    }

    return true;
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
    settings_blob_v1_t blob;
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
 * Load the snapshot from NVS, repairing a missing or damaged entry.
 * Every failure path leaves a complete, readable in-memory
 * configuration behind; none of them aborts the boot.
 */
static esp_err_t settings_load(nvs_handle_t handle)
{
    settings_blob_v1_t blob;
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

    if (!settings_blob_is_valid(&blob, length)) {
        /* Length, version or field range is wrong. The entry belongs to
         * this Service, so only this key is rewritten. */
        ESP_LOGW(TAG, "stored settings invalid (length=%u), restoring defaults",
                 (unsigned)length);
        return settings_install_defaults(handle,
                                        XIAOMIAO_SETTINGS_SOURCE_RECOVERED);
    }

    settings_blob_decode(&blob, &s_settings);
    s_source = XIAOMIAO_SETTINGS_SOURCE_NVS;
    s_last_error = ESP_OK;
    return ESP_OK;
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
        ESP_LOGI(TAG, "settings service ready, source=%s (wifi_auto_connect=%d, sound_enabled=%d)",
                 settings_source_name(s_source), (int)s_settings.wifi_auto_connect,
                 (int)s_settings.sound_enabled);
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
    ESP_LOGI(TAG, "settings stored (wifi_auto_connect=%d, sound_enabled=%d)",
             (int)s_settings.wifi_auto_connect, (int)s_settings.sound_enabled);
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
