/*
 * Settings Service (design doc sections 6, 7 and 12; goal node 9).
 *
 * First System Service. It owns the persisted configuration: the
 * defaults, the validation, the NVS read/write and the fallback used
 * when the stored entry is missing, corrupt or unreadable. Later
 * Services consume it instead of touching NVS themselves, so the
 * firmware has exactly one configuration schema and one recovery
 * policy (goal node 9, "Objective and expected behaviour").
 *
 * The public type carries business fields only. The NVS header, the
 * schema version and the reserved bytes stay private to the
 * implementation.
 *
 * The Service is synchronous and caller-driven: it creates no task,
 * queue, event group or timer, and it references no LVGL, Launcher,
 * Navigation or App symbol (goal node 9, "Out of scope for this node").
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Business fields of the first schema version. Field ranges need no
 * runtime check on the way in: `bool` cannot hold anything but 0 or 1,
 * so a NULL pointer is the only invalid input a caller can hand to
 * xiaomiao_settings_set(). The 0/1 range check still runs on the NVS
 * side, where a damaged blob really can carry 2..255.
 *
 * Both fields are preferences for later nodes and have no effect yet:
 * wifi_auto_connect is consumed by node 10, sound_enabled by node 12.
 * The Settings App therefore offers no switch for either one.
 */
typedef struct {
    bool wifi_auto_connect;
    bool sound_enabled;
} xiaomiao_settings_t;

/*
 * Where the current in-memory snapshot came from. The Settings App's
 * System page displays it; it describes the configuration, not a
 * device fault.
 */
typedef enum {
    XIAOMIAO_SETTINGS_SOURCE_DEFAULTS = 0,
    XIAOMIAO_SETTINGS_SOURCE_NVS,
    XIAOMIAO_SETTINGS_SOURCE_RECOVERED,
    XIAOMIAO_SETTINGS_SOURCE_DEGRADED,
} xiaomiao_settings_source_t;

/*
 * Bring up NVS and load the configuration, installing the defaults
 * when the entry is missing or damaged.
 *
 * Idempotent: only the first call touches NVS, every later call returns
 * the first result unchanged and never rewrites Flash.
 *
 * The Service stays readable after a failure. The snapshot falls back
 * to the defaults, the source becomes
 * XIAOMIAO_SETTINGS_SOURCE_DEGRADED and the raw error is kept for
 * xiaomiao_settings_last_error(). The caller logs the failure and keeps
 * booting: this function never aborts startup and never erases the NVS
 * partition, which later Services will share.
 */
esp_err_t xiaomiao_settings_service_init(void);

/*
 * Copy the current snapshot into *out_settings.
 *
 * Returns ESP_ERR_INVALID_ARG on a NULL pointer and
 * ESP_ERR_INVALID_STATE when the Service was never initialized. The
 * Service copies the whole configuration, so the caller never receives
 * a pointer into the internal state.
 */
esp_err_t xiaomiao_settings_get(xiaomiao_settings_t *out_settings);

/*
 * Validate, persist and then publish a complete configuration.
 *
 * The snapshot is replaced only after the write was committed, so a
 * failed write leaves both Flash and the in-memory state untouched and
 * reports the raw error. Returns ESP_ERR_INVALID_ARG on a NULL pointer
 * and ESP_ERR_INVALID_STATE before initialization.
 */
esp_err_t xiaomiao_settings_set(const xiaomiao_settings_t *settings);

/* Source of the current snapshot; meaningful once init() ran. */
xiaomiao_settings_source_t xiaomiao_settings_source(void);

/* Raw error of the last failed load or write; ESP_OK when there is none. */
esp_err_t xiaomiao_settings_last_error(void);

#ifdef __cplusplus
}
#endif
