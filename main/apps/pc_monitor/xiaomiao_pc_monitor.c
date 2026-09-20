/*
 * PC Monitor skeleton App (goal node 6).
 *
 * The page is created under the Navigation content root and removed by
 * Navigation when the App closes. The App never touches the global
 * screen, never joins the LVGL group and never registers a key handler,
 * so the Launcher root keeps the focus and provides the B path back
 * (goal decisions 5 and 9).
 *
 * Every metric is a compile-time placeholder: the real PC values only
 * exist once the node 11 data channel is defined, so this App must not
 * read ESP32 system data, invent numbers or reserve a protocol shape
 * (goal decisions 6 and 7).
 */

#include "xiaomiao_pc_monitor.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "framework/xiaomiao_navigation.h"

static const char TAG[] = "pc_monitor";

#define PC_MONITOR_APP_ID    "pc_monitor"
#define PC_MONITOR_APP_NAME  "PC Monitor"
/* An eye means "monitoring" without implying that this node is online. */
#define PC_MONITOR_APP_ICON  LV_SYMBOL_EYE_OPEN

/* Same palette as the Launcher so every screen looks like one product. */
#define PC_MONITOR_COLOR_SCREEN_BG 0x0E1016
#define PC_MONITOR_COLOR_TITLE     0xC8D0E0
#define PC_MONITOR_COLOR_LABEL     0x9AA6BC
/* Dimmed because the placeholder values carry no data yet. */
#define PC_MONITOR_COLOR_MUTED     0x5A6478

/*
 * 160 x 128 layout: title strip, four metric rows in two fixed columns
 * and a footer hint. The montserrat_12 line height is 15 px, so the
 * 18 px row step keeps the rows apart and the last row ends well above
 * the footer (goal decision 8).
 */
#define PC_MONITOR_SCREEN_W   160
#define PC_MONITOR_TITLE_Y    4
#define PC_MONITOR_TITLE_H    18
#define PC_MONITOR_ROW_Y0     30
#define PC_MONITOR_ROW_STEP   18
#define PC_MONITOR_ROW_H      16
#define PC_MONITOR_LABEL_X    8
#define PC_MONITOR_LABEL_W    60
#define PC_MONITOR_VALUE_X    92
#define PC_MONITOR_VALUE_W    60
#define PC_MONITOR_HINT_Y     110
#define PC_MONITOR_HINT_H     14

/*
 * The degree sign is written as explicit UTF-8 bytes so the source file
 * stays pure ASCII and no compiler source-charset option can change it.
 * LVGL label text is UTF-8, and the locked montserrat_12 subset covers
 * U+00B0 (checked in lv_font_montserrat_12.c).
 */
#define PC_MONITOR_DEGREE_C  "-- \xC2\xB0" "C"

/* One metric row: the name in the left column, the placeholder on the right. */
typedef struct {
    const char *label;
    const char *value;
} pc_monitor_row_t;

static const pc_monitor_row_t s_rows[] = {
    { "CPU", "-- %" },
    { "RAM", "-- %" },
    { "GPU", "-- %" },
    { "TEMP", PC_MONITOR_DEGREE_C },
};

/* Only the container is kept: it is the double-open guard and the only
 * reference the close path has to drop. The labels belong to Navigation
 * through their parent and are never touched again. */
static lv_obj_t *s_container;

static lv_obj_t *pc_monitor_create_label(lv_obj_t *parent, const char *text,
                                         const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        ESP_LOGW(TAG, "label '%s' allocation failed", text);
        return NULL;
    }

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

/*
 * Give a label an explicit box and text alignment. Fixed columns keep
 * the name and value columns aligned without padding the text with
 * spaces, which cannot align in a proportional font (goal decision 6).
 */
static void pc_monitor_place_box(lv_obj_t *label, int32_t x, int32_t y,
                                 int32_t width, int32_t height,
                                 lv_text_align_t align)
{
    if (label == NULL) {
        return;
    }
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
}

static void pc_monitor_open(void)
{
    if (s_container != NULL) {
        ESP_LOGE(TAG, "pc monitor skeleton is already open");
        return;
    }

    lv_obj_t *root = xiaomiao_navigation_app_root();
    if (root == NULL) {
        ESP_LOGE(TAG, "no App content root to build the skeleton in");
        return;
    }

    lv_obj_t *container = lv_obj_create(root);
    if (container == NULL) {
        ESP_LOGE(TAG, "skeleton container allocation failed");
        return;
    }
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container, lv_color_hex(PC_MONITOR_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    s_container = container;

    lv_obj_t *title = pc_monitor_create_label(container, PC_MONITOR_APP_NAME,
                                              &lv_font_montserrat_14,
                                              PC_MONITOR_COLOR_TITLE);
    pc_monitor_place_box(title, 0, PC_MONITOR_TITLE_Y, PC_MONITOR_SCREEN_W,
                         PC_MONITOR_TITLE_H, LV_TEXT_ALIGN_CENTER);

    for (size_t i = 0; i < sizeof(s_rows) / sizeof(s_rows[0]); ++i) {
        const int32_t y = PC_MONITOR_ROW_Y0 + (int32_t)i * PC_MONITOR_ROW_STEP;

        lv_obj_t *label = pc_monitor_create_label(container, s_rows[i].label,
                                                  &lv_font_montserrat_12,
                                                  PC_MONITOR_COLOR_LABEL);
        pc_monitor_place_box(label, PC_MONITOR_LABEL_X, y, PC_MONITOR_LABEL_W,
                             PC_MONITOR_ROW_H, LV_TEXT_ALIGN_LEFT);

        lv_obj_t *value = pc_monitor_create_label(container, s_rows[i].value,
                                                  &lv_font_montserrat_12,
                                                  PC_MONITOR_COLOR_MUTED);
        pc_monitor_place_box(value, PC_MONITOR_VALUE_X, y, PC_MONITOR_VALUE_W,
                             PC_MONITOR_ROW_H, LV_TEXT_ALIGN_RIGHT);
    }

    lv_obj_t *hint = pc_monitor_create_label(container, "B Back",
                                             &lv_font_montserrat_10,
                                             PC_MONITOR_COLOR_MUTED);
    pc_monitor_place_box(hint, 0, PC_MONITOR_HINT_Y, PC_MONITOR_SCREEN_W,
                         PC_MONITOR_HINT_H, LV_TEXT_ALIGN_CENTER);

    /*
     * Observable for the lifecycle check (goal node 6, decision 13):
     * Navigation released the previous App content root before this
     * callback ran, so the active screen must hold the Launcher root and
     * this content root only. A growing count across entries means a
     * leaked root.
     */
    ESP_LOGI(TAG, "pc monitor opened, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static void pc_monitor_close(void)
{
    /* Navigation deletes the content root after this callback returns;
     * dropping the reference keeps the periodic path off freed objects. */
    s_container = NULL;

    /* Same observable as open, still counting the not-yet-deleted root. */
    ESP_LOGI(TAG, "pc monitor closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static const xiaomiao_app_t s_pc_monitor_app = {
    .id = PC_MONITOR_APP_ID,
    .name = PC_MONITOR_APP_NAME,
    .icon = PC_MONITOR_APP_ICON,
    .init = NULL,
    .open = pc_monitor_open,
    .close = pc_monitor_close,
};

const xiaomiao_app_t *xiaomiao_pc_monitor_app(void)
{
    return &s_pc_monitor_app;
}
