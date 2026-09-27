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

#ifdef __cplusplus
}
#endif
