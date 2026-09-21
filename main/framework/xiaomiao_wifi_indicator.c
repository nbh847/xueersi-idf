/*
 * Framework Wi-Fi indicator implementation (goal node 10, checkpoint 5).
 *
 * Drawing: four two-pixel bars of increasing height plus a small mark
 * slot, inside an 18 x 12 px object pinned to the top-right corner of
 * the top layer. Shape, not only colour, carries the state:
 *
 *   connected, level 4..1   lit bars in green / yellow / orange
 *   connecting              bars light up one after another in blue
 *   radio on, no link       four grey bars, nothing lit
 *   radio off               four dark bars plus "x"
 *   connection failed       four red bars plus "!", for three seconds
 *
 * The timer runs every 200 ms, so a state change is on screen well
 * inside the 500 ms budget the goal asks for.
 */

#include "xiaomiao_wifi_indicator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "services/xiaomiao_wifi_service.h"

static const char TAG[] = "wifi_ind";

/*
 * Geometry. Back to the compact size: an empty slot is expressed by a
 * dark fill that all but disappears into the background, not by an
 * outline. An outline needed 4 px bars and made the whole indicator
 * clumsy, which is worse than the problem it solved.
 */
#define IND_BAR_COUNT   4
#define IND_BAR_WIDTH   2
#define IND_BAR_GAP     1
#define IND_BAR_MIN_H   3
#define IND_BAR_STEP_H  2

#define IND_WIDTH       18
#define IND_HEIGHT      12
#define IND_X           140
#define IND_Y           2
#define IND_MARK_X      (IND_BAR_COUNT * (IND_BAR_WIDTH + IND_BAR_GAP))
#define IND_MARK_W      (IND_WIDTH - IND_MARK_X)

#define IND_POLL_MS     200
/* One animation step for the connecting state. */
#define IND_ANIM_MS     500
/* How long the failure mark stays visible. */
#define IND_FAIL_MS     3000

#define IND_COLOR_GOOD  0x4CAF50
#define IND_COLOR_WEAK  0xFFC107
#define IND_COLOR_POOR  0xFF7043
#define IND_COLOR_BUSY  0x42A5F5
#define IND_COLOR_IDLE  0x9E9E9E
#define IND_COLOR_OFF   0x616161
#define IND_COLOR_FAIL  0xEF5350
#define IND_COLOR_EMPTY 0x2A2F3A

static lv_obj_t *s_root;
static lv_obj_t *s_bars[IND_BAR_COUNT];
static lv_obj_t *s_mark;
static lv_timer_t *s_timer;
static uint32_t s_fail_started_ms;
static uint32_t s_anim_last_ms;
static uint8_t s_anim_lit;

/*
 * Draw the four bars. A bar that carries signal is filled with the state
 * colour; a bar that does not is filled with IND_COLOR_EMPTY, a blue-grey
 * that nearly merges with the dark page background. Filling idle slots
 * with a bright colour was what made "not connected" look like a
 * full-strength reading.
 */
static void indicator_set_bars(uint8_t lit, uint32_t lit_color)
{
    for (uint8_t i = 0; i < IND_BAR_COUNT; ++i) {
        lv_obj_t *bar = s_bars[i];
        if (bar == NULL) {
            continue;
        }

        const bool on = (i < lit);
        lv_obj_set_style_bg_color(bar, lv_color_hex(on ? lit_color : IND_COLOR_EMPTY), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
    }
}

static void indicator_set_mark(const char *text, uint32_t color)
{
    if (s_mark == NULL) {
        return;
    }

    lv_label_set_text(s_mark, text);
    lv_obj_set_style_text_color(s_mark, lv_color_hex(color), 0);
}

static void indicator_show_off(void)
{
    /* Dark slots plus "x": the radio is off. */
    indicator_set_bars(0, IND_COLOR_OFF);
    indicator_set_mark("x", IND_COLOR_OFF);
}

static void indicator_show_idle(void)
{
    /*
     * Dark slots plus a grey "x": the radio is on but nothing is
     * associated. The mark is what makes "not connected" unmistakable at
     * this size; the off state uses the same mark in a darker grey.
     */
    indicator_set_bars(0, IND_COLOR_IDLE);
    indicator_set_mark("x", IND_COLOR_IDLE);
}

static void indicator_show_failure(void)
{
    indicator_set_bars(0, IND_COLOR_FAIL);
    indicator_set_mark("!", IND_COLOR_FAIL);
}

static void indicator_show_connected(uint8_t level)
{
    uint32_t color = IND_COLOR_POOR;

    if (level >= 3) {
        color = IND_COLOR_GOOD;
    }
    else if (level == 2) {
        color = IND_COLOR_WEAK;
    }

    const uint8_t lit = (level == 0) ? 1 : level;
    indicator_set_bars(lit, color);
    indicator_set_mark("", color);
}

static void indicator_show_busy(void)
{
    const uint32_t now = lv_tick_get();
    if (now - s_anim_last_ms >= IND_ANIM_MS) {
        s_anim_last_ms = now;
        s_anim_lit = (uint8_t)((s_anim_lit % IND_BAR_COUNT) + 1);
    }

    indicator_set_bars(s_anim_lit, IND_COLOR_BUSY);
    indicator_set_mark("", IND_COLOR_BUSY);
}

static bool indicator_is_failure(const xiaomiao_wifi_snapshot_t *snapshot)
{
    if (snapshot->state == XIAOMIAO_WIFI_AUTH_FAILED) {
        return true;
    }

    /* A recorded error that is not part of a healthy link counts as a
     * failure the user should notice. */
    return (snapshot->last_error != ESP_OK) &&
           (snapshot->state != XIAOMIAO_WIFI_CONNECTED) &&
           (snapshot->state != XIAOMIAO_WIFI_UNINITIALIZED);
}

static void indicator_refresh(void)
{
    xiaomiao_wifi_snapshot_t snapshot;
    if (xiaomiao_wifi_get_snapshot(&snapshot) != ESP_OK) {
        /* The Service is not up: claim nothing, show the radio as off. */
        indicator_show_off();
        return;
    }

    const uint32_t now = lv_tick_get();
    if (indicator_is_failure(&snapshot)) {
        if (s_fail_started_ms == 0) {
            s_fail_started_ms = (now == 0) ? 1 : now;
        }
        if (now - s_fail_started_ms < IND_FAIL_MS) {
            indicator_show_failure();
            return;
        }
    }
    else {
        s_fail_started_ms = 0;
    }

    switch (snapshot.state) {
    case XIAOMIAO_WIFI_CONNECTED:
        indicator_show_connected(snapshot.signal_level);
        break;
    case XIAOMIAO_WIFI_CONNECTING:
    case XIAOMIAO_WIFI_RETRY_WAIT:
    case XIAOMIAO_WIFI_SCANNING:
        indicator_show_busy();
        break;
    case XIAOMIAO_WIFI_DISABLED:
        indicator_show_off();
        break;
    case XIAOMIAO_WIFI_PROVISIONING:
        /* The hotspot is up and nothing is associated yet: that is the
         * "radio on, no link" shape, not a connection in progress. */
    case XIAOMIAO_WIFI_NO_CREDENTIALS:
    case XIAOMIAO_WIFI_DISCONNECTED:
    case XIAOMIAO_WIFI_AUTH_FAILED:
    case XIAOMIAO_WIFI_ERROR:
    case XIAOMIAO_WIFI_UNINITIALIZED:
    default:
        indicator_show_idle();
        break;
    }
}

static void indicator_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    indicator_refresh();
}

esp_err_t xiaomiao_wifi_indicator_create(void)
{
    if (s_root != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (lv_display_get_default() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * The top layer belongs to no App, so Navigation can open and close
     * whatever it wants without touching the indicator.
     */
    lv_obj_t *root = lv_obj_create(lv_layer_top());
    if (root == NULL) {
        ESP_LOGE(TAG, "indicator allocation failed");
        return ESP_ERR_NO_MEM;
    }

    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, IND_WIDTH, IND_HEIGHT);
    lv_obj_set_pos(root, IND_X, IND_Y);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    /* Decoration only: no focus, no press, no scroll. */
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
    /*
     * A faint dark plate keeps the bars readable on the yellow Hardware
     * Test pages; on the dark pages it is invisible (goal node 10,
     * checkpoint 5).
     */
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_40, 0);
    lv_obj_set_style_radius(root, 2, 0);
    s_root = root;

    for (uint8_t i = 0; i < IND_BAR_COUNT; ++i) {
        lv_obj_t *bar = lv_obj_create(root);
        if (bar == NULL) {
            ESP_LOGW(TAG, "bar allocation failed");
            continue;
        }

        const int32_t height = IND_BAR_MIN_H + (int32_t)i * IND_BAR_STEP_H;
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, IND_BAR_WIDTH, height);
        lv_obj_set_pos(bar, (int32_t)i * (IND_BAR_WIDTH + IND_BAR_GAP),
                       IND_HEIGHT - height);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        s_bars[i] = bar;
    }

    s_mark = lv_label_create(root);
    if (s_mark != NULL) {
        lv_obj_set_style_text_font(s_mark, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(s_mark, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_mark, IND_MARK_X, 0);
        lv_obj_set_size(s_mark, IND_MARK_W, IND_HEIGHT);
        lv_label_set_text(s_mark, "");
    }

    s_timer = lv_timer_create(indicator_timer_cb, IND_POLL_MS, NULL);
    if (s_timer == NULL) {
        ESP_LOGE(TAG, "indicator timer creation failed");
        lv_obj_delete(root);
        s_root = NULL;
        s_mark = NULL;
        return ESP_ERR_NO_MEM;
    }

    indicator_refresh();
    ESP_LOGI(TAG, "indicator created at (%d, %d), %dx%d", IND_X, IND_Y, IND_WIDTH, IND_HEIGHT);
    return ESP_OK;
}

void xiaomiao_wifi_indicator_destroy(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    if (s_root != NULL) {
        lv_obj_delete(s_root);
        s_root = NULL;
    }

    for (uint8_t i = 0; i < IND_BAR_COUNT; ++i) {
        s_bars[i] = NULL;
    }
    s_mark = NULL;
    s_fail_started_ms = 0;
    s_anim_last_ms = 0;
    s_anim_lit = 0;
}
