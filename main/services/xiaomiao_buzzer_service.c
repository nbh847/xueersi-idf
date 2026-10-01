/*
 * Buzzer Service implementation (goal 20261001-1036).
 *
 * The LEDC setup mirrors what the Hardware Test used before the
 * Service existed: low-speed timer 0, 8-bit duty, channel 0 on GPIO14.
 * Only the ownership moved; the Hardware Test pages call the same
 * primitives through the public interface.
 */

#include "xiaomiao_buzzer_service.h"

#include <stdbool.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char TAG[] = "buzzer_svc";

#define BUZZER_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_GPIO         GPIO_NUM_14
#define BUZZER_DUTY         128

/* Alert sequence shape fixed by the design (goal 20261001-1036). */
#define ALERT_BEEP_COUNT    3
#define ALERT_BEEP_MS       150
#define ALERT_GAP_MS        150

typedef enum {
    OWNER_NONE = 0,
    OWNER_MANUAL,
    OWNER_ALERT,
} owner_t;

static bool s_initialized;
static bool s_ready;
static owner_t s_owner;
/* Manual: absolute stop deadline in microseconds. */
static int64_t s_manual_stop_us;
/* Alert: which of the three beeps is on, and when it ends. */
static uint8_t s_alert_step;
static int64_t s_alert_next_us;

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void output_off(void)
{
    ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
}

/* Start one tone; on failure the owner stays NONE so the poll does not
 * keep "stopping" an output that never started. */
static bool tone_start(uint32_t freq_hz)
{
    esp_err_t err = ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "frequency %lu Hz failed: %s",
                 (unsigned long)freq_hz, esp_err_to_name(err));
        return false;
    }

    err = ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY);
    if (err == ESP_OK) {
        err = ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "duty update failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

esp_err_t xiaomiao_buzzer_service_init(void)
{
    if (s_initialized) {
        return s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    s_initialized = true;

    const ledc_timer_config_t timer_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timer init failed: %s", esp_err_to_name(err));
        return err;
    }

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "channel init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    s_owner = OWNER_NONE;
    ESP_LOGI(TAG, "buzzer ready on GPIO%d", (int)BUZZER_GPIO);
    return ESP_OK;
}

bool xiaomiao_buzzer_service_ready(void)
{
    return s_ready;
}

esp_err_t xiaomiao_buzzer_service_beep(uint32_t freq_hz, uint32_t ms)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Manual wins: an alert still playing is cancelled and consumed. */
    if (s_owner == OWNER_ALERT) {
        s_owner = OWNER_NONE;
    }

    if (!tone_start(freq_hz)) {
        s_owner = OWNER_NONE;
        return ESP_FAIL;
    }

    s_owner = OWNER_MANUAL;
    s_manual_stop_us = now_us() + (int64_t)ms * 1000;
    return ESP_OK;
}

void xiaomiao_buzzer_service_stop_manual(void)
{
    if (s_owner != OWNER_MANUAL) {
        return;
    }

    output_off();
    s_owner = OWNER_NONE;
}

esp_err_t xiaomiao_buzzer_service_alert(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    /* A manual output keeps the pin: the reminder is consumed, not
     * queued (goal CP0). A still-playing alert is consumed as well;
     * each completion fires the request exactly once. */
    if (s_owner != OWNER_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    s_owner = OWNER_ALERT;
    s_alert_step = 0;
    s_alert_next_us = 0; /* The first beep starts in poll(). */
    return ESP_OK;
}

bool xiaomiao_buzzer_service_alert_active(void)
{
    return s_owner == OWNER_ALERT;
}

void xiaomiao_buzzer_service_cancel_alert(void)
{
    if (s_owner != OWNER_ALERT) {
        return;
    }

    output_off();
    s_owner = OWNER_NONE;
}

void xiaomiao_buzzer_service_poll(void)
{
    if (s_owner == OWNER_MANUAL) {
        if (now_us() >= s_manual_stop_us) {
            output_off();
            s_owner = OWNER_NONE;
        }
        return;
    }

    if (s_owner != OWNER_ALERT) {
        return;
    }

    const int64_t now = now_us();
    if (s_alert_next_us != 0 && now < s_alert_next_us) {
        return;
    }

    /* Odd steps are gaps, even steps are beeps; the sequence is over
     * after the third beep plus its trailing gap. */
    if (s_alert_step >= ALERT_BEEP_COUNT * 2) {
        output_off();
        s_owner = OWNER_NONE;
        return;
    }

    const bool is_beep = ((s_alert_step % 2) == 0);
    if (is_beep) {
        if (!tone_start(XIAOMIAO_BUZZER_ALERT_FREQ_HZ)) {
            /* Sound hardware broke mid-sequence: end it, never block. */
            output_off();
            s_owner = OWNER_NONE;
            return;
        }
        s_alert_next_us = now + ALERT_BEEP_MS * 1000;
    }
    else {
        output_off();
        s_alert_next_us = now + ALERT_GAP_MS * 1000;
    }
    s_alert_step++;
}
