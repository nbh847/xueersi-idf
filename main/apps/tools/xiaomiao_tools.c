/*
 * Tools App (goal nodes 7, 10, 15C and the 2026-09-30 reorganization).
 *
 * Business App in its transition state: the Wi-Fi, System Info, About
 * and Assets diagnostics moved into the Settings App (Wi-Fi connection
 * details and the System submenu), and the final Pomodoro entry is a
 * separate goal. Until then the page shows the localized "no tools"
 * state only: a title, one line of text and the B-back hint. Nothing
 * here reads hardware, Services or NVS.
 *
 * Input: the App takes the focus on its own root inside the LVGL default
 * group, so LVGL sends it the key events and the Launcher's key handler
 * stays unreachable while Tools is open. Arrows and A have no target,
 * so only B is answered.
 *
 * B has two meanings in one physical key. LVGL dispatches LV_KEY_ESC on
 * the press edge and then again every long_press_repeat_time while B is
 * held, and never sends a release for it, so a press is latched
 * (s_b_latched) and the repeats are ignored. A 20 ms LVGL timer clears
 * the latch once the keypad reports the key released (goal decision 9).
 * Closing the App is deferred to that timer as well, so the focused
 * object is never deleted from inside its own key callback.
 */

#include "xiaomiao_tools.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "framework/xiaomiao_fonts.h"
#include "framework/xiaomiao_i18n.h"
#include "framework/xiaomiao_navigation.h"

static const char TAG[] = "tools";

#define TOOLS_APP_ID    "tools"
/* A list reads as "several entries" and does not clash with the Hardware
 * Test icon (LV_SYMBOL_SETTINGS). LV_SYMBOL_WRENCH does not exist in the
 * locked LVGL 9.5 (goal decision 3). */
#define TOOLS_APP_ICON  LV_SYMBOL_LIST
/* Same palette as the Launcher, PC Monitor and Settings. */
#define TOOLS_COLOR_SCREEN_BG      0x0E1016
#define TOOLS_COLOR_TITLE          0xC8D0E0
#define TOOLS_COLOR_MUTED          0x5A6478

/*
 * 160 x 128 transition layout: title on top, one centered status line,
 * footer hint at the bottom (goal decision 15C).
 */
#define TOOLS_SCREEN_W   160
#define TOOLS_TITLE_Y    2
#define TOOLS_TITLE_H    16
#define TOOLS_EMPTY_Y    56
#define TOOLS_EMPTY_H    16
#define TOOLS_FOOTER_Y   110
#define TOOLS_FOOTER_H   14

/* Poll period for the B release check; the latch itself is event driven. */
#define TOOLS_B_RELEASE_POLL_MS 20

/* The input root owns the focus; the content container is static for
 * the whole App session (goal decisions 6 and 12). */
static lv_obj_t *s_root;
static lv_obj_t *s_content;
static lv_group_t *s_group;
static lv_indev_t *s_keypad;
static lv_timer_t *s_b_release_timer;
static bool s_b_latched;
static bool s_back_pending;

static lv_obj_t *tools_create_label(lv_obj_t *parent, const char *text,
                                    const lv_font_t *font, uint32_t color,
                                    lv_label_long_mode_t mode)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        ESP_LOGW(TAG, "label allocation failed");
        return NULL;
    }

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, mode);
    lv_label_set_text(label, text);
    return label;
}

/* Single-line label with an explicit box; DOTS keeps overlong text inside
 * the box instead of overflowing it (goal decision 14). */
static lv_obj_t *tools_place_label(lv_obj_t *parent, const char *text,
                                   const lv_font_t *font, uint32_t color,
                                   int32_t x, int32_t y, int32_t width,
                                   int32_t height, lv_text_align_t align)
{
    lv_obj_t *label = tools_create_label(parent, text, font, color,
                                         LV_LABEL_LONG_MODE_DOTS);
    if (label == NULL) {
        return NULL;
    }

    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    return label;
}

static void tools_build_empty_state(lv_obj_t *content)
{
    tools_place_label(content, xiaomiao_text(XM_TEXT_APP_TOOLS),
                      xiaomiao_font_small(), TOOLS_COLOR_TITLE, 0,
                      TOOLS_TITLE_Y, TOOLS_SCREEN_W, TOOLS_TITLE_H,
                      LV_TEXT_ALIGN_CENTER);
    tools_place_label(content, xiaomiao_text(XM_TEXT_TOOLS_EMPTY),
                      xiaomiao_font_small(), TOOLS_COLOR_MUTED, 0,
                      TOOLS_EMPTY_Y, TOOLS_SCREEN_W, TOOLS_EMPTY_H,
                      LV_TEXT_ALIGN_CENTER);
    tools_place_label(content, xiaomiao_text(XM_TEXT_HINT_B_BACK),
                      xiaomiao_font_small(), TOOLS_COLOR_MUTED, 0,
                      TOOLS_FOOTER_Y, TOOLS_SCREEN_W, TOOLS_FOOTER_H,
                      LV_TEXT_ALIGN_CENTER);
}

/*
 * One physical B press moves exactly one level. The first ESC event of a
 * press latches, every repeat LVGL sends while B is held is dropped, and
 * the timer clears the latch once the key is released (goal decision 9).
 */
static void tools_handle_escape(void)
{
    if (s_b_latched) {
        return;
    }
    s_b_latched = true;

    /*
     * Transition page: return to the Launcher. Deferred to the timer, so
     * the App root is not deleted while its own key callback is still
     * running.
     */
    s_back_pending = true;
}

static void tools_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) {
        return;
    }

    /* The empty state only answers to B; arrows and A do nothing. */
    if (lv_event_get_key(event) == LV_KEY_ESC) {
        tools_handle_escape();
    }
}

/*
 * Runs from lv_timer_handler(): clears the B latch on release and, when
 * the page asked for it, closes the App from here rather than from the
 * key callback. lv_timer_delete() is safe from the timer's own callback,
 * which is what tools_close() does during that close.
 */
static void tools_b_release_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_b_latched && s_keypad != NULL &&
        lv_indev_get_state(s_keypad) == LV_INDEV_STATE_RELEASED) {
        s_b_latched = false;
    }

    if (s_back_pending) {
        s_back_pending = false;
        const esp_err_t err = xiaomiao_navigation_back();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "return to launcher failed: %s (0x%x)",
                     esp_err_to_name(err), (unsigned)err);
        }
    }
}

static lv_indev_t *tools_find_keypad(void)
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev != NULL;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            return indev;
        }
    }

    return NULL;
}

static void tools_open(void)
{
    if (s_root != NULL) {
        ESP_LOGE(TAG, "tools is already open");
        return;
    }

    lv_obj_t *root_parent = xiaomiao_navigation_app_root();
    if (root_parent == NULL) {
        ESP_LOGE(TAG, "no App content root to build Tools in");
        return;
    }

    lv_group_t *group = lv_group_get_default();
    if (group == NULL) {
        ESP_LOGE(TAG, "LVGL default group is not ready");
        return;
    }

    lv_indev_t *keypad = tools_find_keypad();
    if (keypad == NULL) {
        ESP_LOGE(TAG, "keypad indev missing, cannot detect the B release");
        return;
    }

    lv_obj_t *root = lv_obj_create(root_parent);
    if (root == NULL) {
        ESP_LOGE(TAG, "tools root allocation failed");
        return;
    }
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(TOOLS_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    s_root = root;

    lv_obj_t *content = lv_obj_create(root);
    if (content == NULL) {
        ESP_LOGE(TAG, "tools content allocation failed");
        lv_obj_delete(root);
        s_root = NULL;
        return;
    }
    lv_obj_remove_style_all(content);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    s_content = content;

    lv_timer_t *timer = lv_timer_create(tools_b_release_timer_cb,
                                        TOOLS_B_RELEASE_POLL_MS, NULL);
    if (timer == NULL) {
        ESP_LOGE(TAG, "B release timer creation failed");
        lv_obj_delete(root);
        s_root = NULL;
        s_content = NULL;
        return;
    }
    s_b_release_timer = timer;

    s_group = group;
    s_keypad = keypad;
    s_b_latched = false;
    s_back_pending = false;

    lv_obj_add_event_cb(s_root, tools_key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(s_group, s_root);
    lv_group_focus_obj(s_root);

    tools_build_empty_state(s_content);

    ESP_LOGI(TAG, "tools opened, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static void tools_close(void)
{
    /* Own resources first, then hand the object tree back to Navigation,
     * which deletes the content root after this callback returns
     * (goal decision 17). */
    if (s_b_release_timer != NULL) {
        lv_timer_delete(s_b_release_timer);
        s_b_release_timer = NULL;
    }

    if (s_root != NULL && s_group != NULL) {
        lv_group_remove_obj(s_root);
    }

    s_content = NULL;
    s_root = NULL;
    s_group = NULL;
    s_keypad = NULL;
    s_b_latched = false;
    s_back_pending = false;

    ESP_LOGI(TAG, "tools closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static xiaomiao_app_t s_tools_app = {
    .id = TOOLS_APP_ID,
    .name = NULL,
    .icon = TOOLS_APP_ICON,
    .init = NULL,
    .open = tools_open,
    .close = tools_close,
};

const xiaomiao_app_t *xiaomiao_tools_app(void)
{
    /* The Launcher registers apps after the Font Service has run, so the
     * localized name is resolved on the first access. */
    s_tools_app.name = xiaomiao_text(XM_TEXT_APP_TOOLS);
    return &s_tools_app;
}
