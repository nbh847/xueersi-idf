/*
 * Buzzer Service (goal 20261001-1036, "sound and resource ownership").
 *
 * Single owner of GPIO14 and LEDC low-speed timer 0 / channel 0. The
 * Hardware Test beep and the Tools Pomodoro completion reminder both go
 * through this Service, so exactly one driver drives the pin and the
 * two users cannot fight over the LEDC channel.
 *
 * Ownership rules (goal CP0 decision):
 *   - A manual beep preempts an alert sequence: the remaining alert is
 *     cancelled and never replayed.
 *   - An alert request is silently refused while a manual beep is
 *     running: the reminder is consumed, not queued, and not replayed
 *     after the manual output ends.
 *   - Cancelling the alert only touches alert-owned output; it can
 *     never stop a manual beep. Stopping the manual output only
 *     touches manual output. Both are idempotent.
 *
 * The alert sequence is three short beeps (default 988 Hz, about
 * 150 ms on / 150 ms off). It is advanced by xiaomiao_buzzer_poll(),
 * which the UI main loop calls every iteration; nothing here blocks,
 * creates a task or uses LVGL, and a beep that outlives its owner is
 * stopped by the poll as well.
 *
 * Threading: init() runs once from app_main before the LVGL task
 * starts; every other call happens on the UI task. No locks.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Frequency the goal fixes as the reminder starting point. */
#define XIAOMIAO_BUZZER_ALERT_FREQ_HZ 988

/*
 * Configure LEDC timer 0 / channel 0 on GPIO14. Idempotent. On failure
 * the Service reports not-ready: beeps and alerts become no-ops, the
 * boot and the timer keep working, and the raw error is returned once.
 */
esp_err_t xiaomiao_buzzer_service_init(void);

/* True once the LEDC channel was configured successfully. */
bool xiaomiao_buzzer_service_ready(void);

/*
 * Manual output (Hardware Test and UI feedback). Preempts a running
 * alert. Returns ESP_ERR_INVALID_STATE when the Service is not ready.
 */
esp_err_t xiaomiao_buzzer_service_beep(uint32_t freq_hz, uint32_t ms);

/* Stop the manual output; no effect on an alert and safe to repeat. */
void xiaomiao_buzzer_service_stop_manual(void);

/*
 * Fire the three-beep completion reminder. Refused (ESP_ERR_INVALID_STATE)
 * while a manual beep runs or another alert is still playing; the
 * caller treats both as "consumed, no replay".
 */
esp_err_t xiaomiao_buzzer_service_alert(void);

/* True while the alert sequence owns the output. */
bool xiaomiao_buzzer_service_alert_active(void);

/*
 * Cancel a running alert (both sound switches off, or a page that owns
 * the reminder going away). Manual output is untouched. Safe to repeat.
 */
void xiaomiao_buzzer_service_cancel_alert(void);

/*
 * Advance the timing: stop an expired manual beep, step the alert
 * sequence. Called from the UI main loop; non-blocking.
 */
void xiaomiao_buzzer_service_poll(void);

#ifdef __cplusplus
}
#endif
