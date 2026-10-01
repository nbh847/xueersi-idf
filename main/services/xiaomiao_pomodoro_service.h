/*
 * Pomodoro Service (goal 20261001-1036).
 *
 * Owns the timer state machine: the user starts a focus phase; when it
 * ends, the configured break phase starts automatically (2026-10-01
 * flow decision - the state never rests in FOCUS_DONE). When the break
 * ends the cycle stops and the page offers another round. The state
 * lives independently of the Tools App: leaving the App keeps the
 * timer running, re-entering shows the exact remaining time, and a
 * reboot returns to idle (the timer state is never written to NVS).
 *
 * Timing uses the 64-bit monotonic clock as a deadline difference, so
 * no wall-clock or per-second countdown drifts; paused time is frozen
 * by storing the remaining duration and setting a new deadline on
 * resume. Reaching zero converts exactly once per phase and emits one
 * completion event; the caller cannot read the event twice.
 *
 * The Service is caller-driven and single-threaded: init() runs from
 * app_main before the LVGL task starts, every other call happens on
 * the UI task (page code or the main-loop poll). No task, queue,
 * timer, mutex, LVGL or NVS dependency.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fallback phase lengths, used only when the Settings Service is
 * degraded. The lengths the user configures in the Tools setup page
 * (Settings schema v3 focus_minutes / break_minutes, 2026-10-01 scope
 * extension) take precedence and are read at every phase start.
 */
#define XIAOMIAO_POMODORO_FOCUS_MS (25U * 60U * 1000U)
#define XIAOMIAO_POMODORO_BREAK_MS (5U * 60U * 1000U)

typedef enum {
    XIAOMIAO_POMODORO_IDLE = 0,
    XIAOMIAO_POMODORO_FOCUS_RUNNING,
    XIAOMIAO_POMODORO_FOCUS_PAUSED,
    /* Transient only: poll() reports focus completion through the
     * one-shot event and immediately starts the break. */
    XIAOMIAO_POMODORO_FOCUS_DONE,
    XIAOMIAO_POMODORO_BREAK_RUNNING,
    XIAOMIAO_POMODORO_BREAK_PAUSED,
    XIAOMIAO_POMODORO_BREAK_DONE,
} xiaomiao_pomodoro_state_t;

typedef struct {
    xiaomiao_pomodoro_state_t state;
    /* Total length of the current phase (also set in idle: the focus
     * length, so the untouched page can draw the full ring). */
    uint32_t total_ms;
    /* Remaining time of the current phase, computed at read time.
     * Zero once done; in idle it equals total_ms. */
    uint32_t remaining_ms;
    /* Remaining share of the current phase, 0..100. */
    uint8_t progress_percent;
} xiaomiao_pomodoro_snapshot_t;

/*
 * Reset to idle. Never fails; safe to call again at any time.
 */
esp_err_t xiaomiao_pomodoro_service_init(void);

/* Begin a focus / break phase from idle, paused or a done state. */
void xiaomiao_pomodoro_service_start_focus(void);
void xiaomiao_pomodoro_service_start_break(void);

/* Only a running phase can pause; only a paused phase can resume. */
void xiaomiao_pomodoro_service_pause(void);
void xiaomiao_pomodoro_service_resume(void);

/*
 * Back to idle from any state. Unconditional here: the confirmation
 * step lives in the App, the Service just resets.
 */
void xiaomiao_pomodoro_service_reset(void);

/* Copy a self-consistent view of the timer; NULL is rejected. */
esp_err_t xiaomiao_pomodoro_service_get_snapshot(
    xiaomiao_pomodoro_snapshot_t *out_snapshot);

/*
 * Main-loop hook: converts an expired deadline into the done state,
 * fires the one-shot completion event and (with both sound switches
 * on) asks the Buzzer Service for the three-beep reminder. Non-blocking.
 */
void xiaomiao_pomodoro_service_poll(void);

/*
 * Read and clear the one-shot completion event. Returns true once per
 * completed phase; *out_state is then FOCUS_DONE or BREAK_DONE.
 */
bool xiaomiao_pomodoro_service_take_completed(
    xiaomiao_pomodoro_state_t *out_state);

#ifdef __cplusplus
}
#endif
