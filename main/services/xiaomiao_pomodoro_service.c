/*
 * Pomodoro Service implementation (goal 20261001-1036).
 *
 * Deadline arithmetic: a running phase stores an absolute deadline in
 * microseconds on the 64-bit monotonic clock. Remaining time is always
 * max(0, deadline - now), so the displayed value cannot drift and a
 * page that was closed for minutes re-enters with the exact remainder.
 * Pausing stores the remaining duration; resuming sets a fresh
 * deadline.
 *
 * Reaching zero happens in poll(): the state flips to done exactly
 * once, the completion event is latched for one reader, and the
 * reminder is requested from the Buzzer Service when both the pomodoro
 * preference and the system sound switch are on. A refused reminder
 * (manual output running) is consumed: no queueing, no replay.
 */

#include "xiaomiao_pomodoro_service.h"

#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "xiaomiao_buzzer_service.h"
#include "xiaomiao_settings_service.h"

static const char TAG[] = "pomodoro_svc";

static xiaomiao_pomodoro_state_t s_state;
/* Absolute deadline while running; remaining duration while paused. */
static int64_t s_deadline_us;
static uint32_t s_total_ms;
static uint32_t s_remaining_ms;
static bool s_completed_pending;
static xiaomiao_pomodoro_state_t s_completed_stage;

/*
 * Phase length configured in the Settings App (schema v3
 * focus_minutes / break_minutes). Falls back to the compile-time
 * defaults when the Settings Service is degraded, which keeps the
 * timer usable even on a broken NVS.
 */
static uint32_t phase_total_ms(bool focus)
{
    xiaomiao_settings_t settings;
    if (xiaomiao_settings_get(&settings) == ESP_OK) {
        const uint32_t minutes = focus ? settings.focus_minutes
                                       : settings.break_minutes;
        const uint32_t max_minutes = focus ? 180U : 60U;
        if (minutes >= 1U && minutes <= max_minutes) {
            return minutes * 60000U;
        }
    }

    return focus ? XIAOMIAO_POMODORO_FOCUS_MS : XIAOMIAO_POMODORO_BREAK_MS;
}

static void phase_complete(xiaomiao_pomodoro_state_t done_state)
{
    s_completed_pending = true;
    s_completed_stage = done_state;

    ESP_LOGI(TAG, "phase done: %s",
             done_state == XIAOMIAO_POMODORO_FOCUS_DONE ? "focus" : "break");

    /* Two-level switch: the pomodoro preference and the system master.
     * A read failure keeps the previous snapshot's behaviour, and the
     * degraded default (both on) still only beeps three times once. */
    xiaomiao_settings_t settings;
    if (xiaomiao_settings_get(&settings) == ESP_OK &&
        settings.sound_enabled && settings.pomodoro_sound_enabled) {
        /* Refused while a manual output runs: consumed, no replay. */
        (void)xiaomiao_buzzer_service_alert();
    }
}

esp_err_t xiaomiao_pomodoro_service_init(void)
{
    s_state = XIAOMIAO_POMODORO_IDLE;
    s_deadline_us = 0;
    s_total_ms = phase_total_ms(true);
    s_completed_pending = false;
    s_completed_stage = XIAOMIAO_POMODORO_IDLE;
    return ESP_OK;
}

static void start_phase(xiaomiao_pomodoro_state_t running_state,
                        uint32_t total_ms)
{
    s_state = running_state;
    s_total_ms = total_ms;
    s_deadline_us = esp_timer_get_time() + (int64_t)total_ms * 1000;
    /* Starting a new phase discards a completion event the user never
     * read: the phase it reported is over and was acknowledged by the
     * explicit start action. */
    s_completed_pending = false;
}

void xiaomiao_pomodoro_service_start_focus(void)
{
    /* The length is read at start time, so a duration change in the
     * setup page applies to the next phase; a running phase keeps its
     * deadline. */
    start_phase(XIAOMIAO_POMODORO_FOCUS_RUNNING, phase_total_ms(true));
}

void xiaomiao_pomodoro_service_start_break(void)
{
    start_phase(XIAOMIAO_POMODORO_BREAK_RUNNING, phase_total_ms(false));
}

void xiaomiao_pomodoro_service_pause(void)
{
    if (s_state != XIAOMIAO_POMODORO_FOCUS_RUNNING &&
        s_state != XIAOMIAO_POMODORO_BREAK_RUNNING) {
        return;
    }

    const int64_t remaining_us =
        s_deadline_us - esp_timer_get_time();
    const uint32_t remaining_ms =
        (remaining_us > 0) ? (uint32_t)(remaining_us / 1000) : 0;

    s_state = (s_state == XIAOMIAO_POMODORO_FOCUS_RUNNING)
                  ? XIAOMIAO_POMODORO_FOCUS_PAUSED
                  : XIAOMIAO_POMODORO_BREAK_PAUSED;
    s_deadline_us = 0;
    s_remaining_ms = remaining_ms;
}

void xiaomiao_pomodoro_service_resume(void)
{
    if (s_state != XIAOMIAO_POMODORO_FOCUS_PAUSED &&
        s_state != XIAOMIAO_POMODORO_BREAK_PAUSED) {
        return;
    }

    const uint32_t remaining_ms = s_remaining_ms;
    s_state = (s_state == XIAOMIAO_POMODORO_FOCUS_PAUSED)
                  ? XIAOMIAO_POMODORO_FOCUS_RUNNING
                  : XIAOMIAO_POMODORO_BREAK_RUNNING;
    /* A resume at exactly zero would leave the poll to convert the
     * phase immediately, which is the correct one-shot semantics. */
    s_deadline_us = esp_timer_get_time() + (int64_t)remaining_ms * 1000;
}

void xiaomiao_pomodoro_service_reset(void)
{
    s_state = XIAOMIAO_POMODORO_IDLE;
    s_deadline_us = 0;
    s_total_ms = phase_total_ms(true);
    s_remaining_ms = 0;
    s_completed_pending = false;
}

void xiaomiao_pomodoro_service_poll(void)
{
    if (s_state == XIAOMIAO_POMODORO_FOCUS_RUNNING &&
        esp_timer_get_time() >= s_deadline_us) {
        /* Focus end: the configured break starts automatically - the
         * user no longer switches into it by hand (2026-10-01
         * decision). The state never rests in FOCUS_DONE. start_phase
         * runs first because it discards a stale event; only then is
         * the fresh focus-completion event latched and the reminder
         * requested. */
        start_phase(XIAOMIAO_POMODORO_BREAK_RUNNING, phase_total_ms(false));
        phase_complete(XIAOMIAO_POMODORO_FOCUS_DONE);
        return;
    }

    if (s_state == XIAOMIAO_POMODORO_BREAK_RUNNING &&
        esp_timer_get_time() >= s_deadline_us) {
        /* Break end: the cycle stops here. The page offers "again"
         * (a fresh focus); leaving the state as-is keeps the result
         * visible until the user starts or resets. */
        phase_complete(XIAOMIAO_POMODORO_BREAK_DONE);
        s_state = XIAOMIAO_POMODORO_BREAK_DONE;
        s_deadline_us = 0;
    }
}

esp_err_t xiaomiao_pomodoro_service_get_snapshot(
    xiaomiao_pomodoro_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_snapshot->state = s_state;

    switch (s_state) {
    case XIAOMIAO_POMODORO_IDLE: {
        /* Idle mirrors the configured focus length so the untouched
         * page draws the full ring for the next phase. */
        const uint32_t total = phase_total_ms(true);
        out_snapshot->total_ms = total;
        out_snapshot->remaining_ms = total;
        out_snapshot->progress_percent = 100;
        break;
    }
    case XIAOMIAO_POMODORO_FOCUS_RUNNING:
    case XIAOMIAO_POMODORO_BREAK_RUNNING: {
        const int64_t remaining_us = s_deadline_us - esp_timer_get_time();
        const uint32_t remaining_ms =
            (remaining_us > 0) ? (uint32_t)(remaining_us / 1000) : 0;
        out_snapshot->total_ms = s_total_ms;
        out_snapshot->remaining_ms = remaining_ms;
        out_snapshot->progress_percent =
            (uint8_t)((remaining_ms * 100U) / s_total_ms);
        break;
    }
    case XIAOMIAO_POMODORO_FOCUS_PAUSED:
    case XIAOMIAO_POMODORO_BREAK_PAUSED:
        out_snapshot->total_ms = s_total_ms;
        out_snapshot->remaining_ms = s_remaining_ms;
        out_snapshot->progress_percent =
            (uint8_t)((s_remaining_ms * 100U) / s_total_ms);
        break;
    case XIAOMIAO_POMODORO_FOCUS_DONE:
    case XIAOMIAO_POMODORO_BREAK_DONE:
    default:
        out_snapshot->total_ms = s_total_ms;
        out_snapshot->remaining_ms = 0;
        out_snapshot->progress_percent = 0;
        break;
    }

    return ESP_OK;
}

bool xiaomiao_pomodoro_service_take_completed(
    xiaomiao_pomodoro_state_t *out_state)
{
    if (!s_completed_pending) {
        return false;
    }

    s_completed_pending = false;
    if (out_state != NULL) {
        *out_state = s_completed_stage;
    }
    return true;
}
