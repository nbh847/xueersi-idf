/*
 * Time Service (goal 20261001-1657).
 *
 * Sole owner of SNTP and system-time synchronization. Wi-Fi keeps the
 * wireless lifecycle; the Launcher clock only reads the snapshot from
 * here, and no other firmware file calls the `esp_netif_sntp_*`
 * interfaces (AGENTS.md, Service ownership).
 *
 * The service never blocks the UI thread: poll() runs once per UI loop
 * iteration but acts at most once per second, and only uses
 * non-waiting control calls. Every boot starts without a valid time;
 * the clock is shown only after a legal SNTP sync in this boot and
 * stays shown for at most 24 hours (measured on the 64-bit monotonic
 * clock, never on the SNTP-corrected wall time).
 *
 * Snapshot contract (goal "确定 decision: Service 与同步"):
 * - get_snapshot(NULL)              -> ESP_ERR_INVALID_ARG
 * - service not initialized         -> snapshot cleared, ESP_ERR_INVALID_STATE
 * - initialization failed           -> snapshot cleared, saved error
 * - never synced / stale / illegal  -> snapshot cleared, ESP_ERR_INVALID_STATE
 * - success                         -> valid=true, "YYYY-MM-DD HH:MM" (UTC+8)
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "YYYY-MM-DD HH:MM": 16 visible ASCII characters plus the NUL. */
#define XIAOMIAO_TIME_DATETIME_BUF 17

typedef struct {
    bool valid;
    char datetime[XIAOMIAO_TIME_DATETIME_BUF];
} xiaomiao_time_snapshot_t;

/*
 * Bring the service up. Idempotent: later calls return the first
 * result. Registers the SNTP sync notification and creates the SNTP
 * instance in stopped state (config.start = false); the actual start
 * happens in poll() once Wi-Fi reports a lease. Never waits for the
 * network or for the first sync; a failure is logged, kept for the
 * snapshot, retried by poll() and never blocks the boot.
 */
esp_err_t xiaomiao_time_service_init(void);

/*
 * Drive the network state machine. Called from the normal UI loop;
 * internally throttled to 1 Hz. Starts SNTP when Wi-Fi is connected
 * with a non-zero lease, tears the SNTP instance down when the network
 * goes away, and rebuilds it once when the IPv4 address changes.
 * Stopping SNTP never clears a time that is still inside its 24-hour
 * validity window.
 */
void xiaomiao_time_service_poll(void);

/*
 * Copy the current display snapshot. Always produces a NUL-terminated
 * string; on every error path the output is cleared and the caller
 * must treat the time as invalid. Formatting happens outside the
 * critical section; the wall-time sample is taken after the sync
 * metadata (SNTP sets the system time before the sync callback runs,
 * so a sample after the metadata is never older than the sync point).
 */
esp_err_t xiaomiao_time_get_snapshot(xiaomiao_time_snapshot_t *out);

#ifdef __cplusplus
}
#endif
