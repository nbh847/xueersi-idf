/*
 * Agent Service (goal node 11).
 *
 * Third System Service. It owns LAN discovery and the HTTP link to the
 * unified Xiaomiao Agent on the PC: Wi-Fi readiness, UDP discovery,
 * periodic fetch, response size limit, JSON validation and the
 * thread-safe PC metrics snapshot (goal node 11 and discovery follow-up).
 *
 * Layering follows the existing Services: Apps and the Framework only
 * call this interface; they never include esp_http_client.h, esp_wifi.h,
 * cJSON.h or NVS headers, and this Service never calls esp_wifi_* itself
 * - the Wi-Fi Service stays the single source of connectivity truth via
 * xiaomiao_wifi_get_snapshot().
 *
 * The Service is asynchronous: init() starts one background Worker,
 * then returns without waiting for Wi-Fi, discovery, HTTP or the first
 * sample, so the Launcher can never be delayed (goal node 11,
 * "Worker scheduling"). Callers read progress through
 * xiaomiao_agent_get_snapshot().
 *
 * Properties of the public snapshot:
 * - Every metric carries its own valid flag; a numeric 0 is a real
 *   measurement and never means "unknown" (goal decision 7).
 * - has_valid_metrics turns false once 3 seconds pass without a new
 *   valid core snapshot; the UI must show `--` again instead of stale
 *   values (goal node 11, "3-second rule").
 * - get_snapshot() copies one consistent view under the Service lock.
 *   It stays readable after a failed init (state ERROR) and before
 *   initialization (ESP_ERR_INVALID_STATE).
 *
 * AI quota pages goal (2026-09-27) added the per-provider quota
 * snapshots. They reuse the same discovered Agent host and port, but own
 * a low-frequency schedule, a 180-second freshness window and their own
 * state, so PC metrics stay on the 1-second / 3-second rules and a quota
 * round never delays or resets them (and neither provider affects the
 * other).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lifecycle of the Agent link. The Worker waits for Wi-Fi, discovers an
 * Agent over the local network, then polls its HTTP API. ERROR means init
 * failed; the device keeps booting and the snapshot stays readable.
 */
typedef enum {
    XIAOMIAO_AGENT_UNINITIALIZED = 0,
    XIAOMIAO_AGENT_WIFI_OFFLINE,
    XIAOMIAO_AGENT_DISCOVERING,
    XIAOMIAO_AGENT_CONNECTING,
    XIAOMIAO_AGENT_ONLINE,
    XIAOMIAO_AGENT_DEGRADED,
    XIAOMIAO_AGENT_RETRY_WAIT,
    XIAOMIAO_AGENT_ERROR,
} xiaomiao_agent_state_t;

/*
 * One consistent view of the Agent link and the last PC metrics. The
 * *_valid flags only tell whether the metric was part of the last valid
 * core snapshot AND the 3-second validity window is still open; the
 * float fields keep their last values for diagnostics and are ignored
 * by callers whenever the matching flag is false.
 */
typedef struct {
    xiaomiao_agent_state_t state;
    bool has_valid_metrics;
    bool cpu_valid;
    bool memory_valid;
    bool gpu_valid;
    bool cpu_temperature_valid;
    bool gpu_temperature_valid;
    float cpu_percent;
    float memory_percent;
    float gpu_percent;
    float cpu_temperature_c;
    float gpu_temperature_c;
    /* Monotonic device time of the last committed valid snapshot, in
     * microseconds; 0 when nothing was ever committed. */
    int64_t last_success_us;
    uint32_t consecutive_failures;
    int http_status;
    esp_err_t last_error;
} xiaomiao_agent_snapshot_t;

/*
 * Fixed provider set of the quota routes; the device matches windows by
 * provider ID and label and never by array order (goal decision 2).
 */
typedef enum {
    XIAOMIAO_QUOTA_PROVIDER_CODEX = 0,
    XIAOMIAO_QUOTA_PROVIDER_ZHIPU = 1,
    XIAOMIAO_QUOTA_PROVIDER_COUNT,
} xiaomiao_quota_provider_t;

/*
 * Display state of one provider: OK, LOGIN, SRC, BAD, STALE and OFF, in
 * the exact terms the monitor UI uses. OFFLINE covers both "no Wi-Fi or
 * no discovered Agent" and "the quota request itself could not be sent".
 */
typedef enum {
    XIAOMIAO_QUOTA_OFFLINE = 0,
    XIAOMIAO_QUOTA_OK,
    XIAOMIAO_QUOTA_AUTH_REQUIRED,
    XIAOMIAO_QUOTA_UNAVAILABLE,
    XIAOMIAO_QUOTA_INVALID_DATA,
    XIAOMIAO_QUOTA_STALE,
} xiaomiao_quota_state_t;

/* Each provider always reports these two windows, in this order. */
#define XIAOMIAO_QUOTA_WINDOWS 2
#define XIAOMIAO_QUOTA_LABEL_MAX 8
/* "MM-DD HH:MM" plus the terminator. */
#define XIAOMIAO_QUOTA_TIME_MAX 12

/*
 * One quota window. ``present`` is false when the last successful round
 * did not carry that label; the UI then shows `--` for the whole row.
 * A numeric zero in ``remaining_percent`` is real data and never means
 * "unknown" (goal decision 3).
 */
typedef struct {
    bool present;
    char label[XIAOMIAO_QUOTA_LABEL_MAX];
    bool remaining_valid;
    uint8_t remaining_percent;
    bool reset_time_valid;
    char reset_at_local[XIAOMIAO_QUOTA_TIME_MAX];
    bool reset_countdown_valid;
    /* Remaining seconds at commit time, from the PC clock. The UI counts
     * this down with the device monotonic clock and shows the pending
     * state once it reaches zero (goal decision 3). */
    int64_t reset_in_sec;
} xiaomiao_quota_window_t;

/*
 * One consistent view of a provider quota. The window slots always hold
 * the provider's two canonical labels; only ``present`` and the validity
 * flags vary. ``received_us`` is the monotonic device time of the last
 * successful commit, 0 when nothing was ever committed, and is what the
 * UI uses to advance the countdown.
 */
typedef struct {
    xiaomiao_quota_state_t state;
    bool has_snapshot;
    uint8_t window_count;
    xiaomiao_quota_window_t windows[XIAOMIAO_QUOTA_WINDOWS];
    int64_t received_us;
    int http_status;
    esp_err_t last_error;
} xiaomiao_quota_snapshot_t;

/*
 * Start the background discovery and polling Worker.
 *
 * Idempotent: only the first call starts anything, later calls return
 * the first result. Never blocks on Wi-Fi, discovery, HTTP or the first
 * sample, and never aborts startup: a failure leaves state ERROR and
 * the boot continues (goal node 11, "Worker scheduling").
 */
esp_err_t xiaomiao_agent_service_init(void);

/*
 * Copy the current snapshot into *out_snapshot.
 *
 * Returns ESP_ERR_INVALID_ARG on a NULL pointer and
 * ESP_ERR_INVALID_STATE when the Service was never initialized. The
 * copy is taken under the Service lock, so state and metrics always
 * belong to the same moment; no internal pointer or HTTP body is
 * exposed (goal node 11, "State model").
 */
esp_err_t xiaomiao_agent_get_snapshot(xiaomiao_agent_snapshot_t *out_snapshot);

/*
 * Copy the current quota snapshot of one provider into *out_snapshot.
 *
 * Returns ESP_ERR_INVALID_ARG on a NULL pointer or an unknown provider
 * and ESP_ERR_INVALID_STATE when the Service was never initialized.
 * Readable after a failed init. Windows are always the provider's two
 * canonical labels; a provider that never had a successful round reads
 * back with has_snapshot false and empty windows.
 *
 * The reported state already applies the 180-second quota freshness
 * window: a successful snapshot older than that reads as STALE, while
 * an unsent request, lost Agent link or missing Wi-Fi reads as OFFLINE.
 * The 3-second PC metrics rule does not apply here.
 */
esp_err_t xiaomiao_agent_get_quota_snapshot(xiaomiao_quota_provider_t provider,
                                            xiaomiao_quota_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
