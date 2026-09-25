/*
 * PC Monitor App (goal nodes 6, 11 and 12).
 *
 * Two pages, each with live numeric values and a 60-second rolling chart:
 *   page 0 - CPU + RAM percent (shared 0..100 scale)
 *   page 1 - GPU percent + GPU temperature (shared 0..150 scale)
 * RIGHT/LEFT switch pages, B returns to the Launcher. The App joins the
 * LVGL default group on open (like the Settings App) so it receives key
 * events directly; the Launcher root keeps no focus while an App is open.
 *
 * Node 11 feeds the Agent Service snapshot into both the value labels and
 * the chart series. Missing data keeps the node 6 look ("--") and pushes
 * LV_CHART_POINT_NONE so the line has a gap instead of a false zero. The
 * 3-second validity rule lives in the Service, not here.
 *
 * The App holds no network object of its own, never includes
 * esp_http_client/esp_wifi/cJSON headers and formats values itself.
 */

#include "xiaomiao_pc_monitor.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "framework/xiaomiao_app.h"
#include "framework/xiaomiao_fonts.h"
#include "framework/xiaomiao_i18n.h"
#include "framework/xiaomiao_navigation.h"
#include "services/xiaomiao_agent_service.h"

static const char TAG[] = "pc_monitor";

#define PC_MONITOR_APP_ID    "pc_monitor"
#define PC_MONITOR_APP_ICON  LV_SYMBOL_EYE_OPEN

#define PC_MONITOR_COLOR_SCREEN_BG 0x0E1016
#define PC_MONITOR_COLOR_TITLE     0xC8D0E0
#define PC_MONITOR_COLOR_LABEL     0x9AA6BC
#define PC_MONITOR_COLOR_VALUE     0xE0E8F0
#define PC_MONITOR_COLOR_MUTED     0x5A6478
#define PC_MONITOR_COLOR_GRID      0x3A4258
#define PC_MONITOR_COLOR_CPU       0x00C8FF
#define PC_MONITOR_COLOR_RAM       0xFFB030
#define PC_MONITOR_COLOR_GPU       0x30D060
#define PC_MONITOR_COLOR_TEMP      0xFF5050

#define PC_MONITOR_SCREEN_W   160
#define PC_MONITOR_HISTORY    60
#define PC_MONITOR_TIMER_MS   20
#define PC_MONITOR_REFRESH_TICKS (1000 / PC_MONITOR_TIMER_MS)

#define PC_MONITOR_DEGREE_UTF8 "\xC2\xB0"
#define PC_MONITOR_DEGREE_C  "-- \xC2\xB0" "C"

typedef enum {
    PC_MONITOR_PAGE_CPU_RAM = 0,
    PC_MONITOR_PAGE_GPU_TEMP,
    PC_MONITOR_PAGE_COUNT,
} pc_monitor_page_t;

static lv_obj_t *s_container;
static lv_obj_t *s_pages[PC_MONITOR_PAGE_COUNT];
static lv_obj_t *s_title_label;
static lv_obj_t *s_name_labels[PC_MONITOR_PAGE_COUNT][2];
static lv_obj_t *s_value_labels[PC_MONITOR_PAGE_COUNT][2];
static lv_obj_t *s_charts[PC_MONITOR_PAGE_COUNT];
static lv_chart_series_t *s_series[PC_MONITOR_PAGE_COUNT][2];
static int32_t s_last_value[PC_MONITOR_PAGE_COUNT][2];
static lv_obj_t *s_hint_label;
static lv_timer_t *s_timer;
static lv_group_t *s_group;
static lv_indev_t *s_keypad;
static pc_monitor_page_t s_current_page;
static bool s_back_pending;
static bool s_b_latched;
static size_t s_refresh_ticks;

static const char *pc_monitor_page_title(pc_monitor_page_t page)
{
    /*
     * One ID per page title. "CPU / RAM" is all technical abbreviation
     * and stays identical in both languages; only "Temp" is localized.
     */
    static const xiaomiao_text_id_t titles[PC_MONITOR_PAGE_COUNT] = {
        [PC_MONITOR_PAGE_CPU_RAM] = XM_TEXT_PM_TITLE_CPU_RAM,
        [PC_MONITOR_PAGE_GPU_TEMP] = XM_TEXT_PM_TITLE_GPU_TEMP,
    };

    return xiaomiao_text(titles[page]);
}

static lv_indev_t *pc_monitor_find_keypad(void)
{
    lv_indev_t *indev = NULL;
    for (;;) {
        indev = lv_indev_get_next(indev);
        if (indev == NULL) {
            break;
        }
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            return indev;
        }
    }
    return NULL;
}

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

static void pc_monitor_place(lv_obj_t *label, int32_t x, int32_t y,
                             int32_t w, int32_t h, lv_text_align_t align)
{
    if (label == NULL) {
        return;
    }
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
}

static void pc_monitor_format_value(float value, const char *suffix,
                                    char *out, size_t size)
{
    int scaled = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    int whole = scaled / 10;
    int fraction = scaled % 10;
    if (fraction < 0) {
        fraction = -fraction;
    }
    if (whole > 999) {
        whole = 999;
    } else if (whole < -999) {
        whole = -999;
    }
    snprintf(out, size, "%d.%d%s", whole, fraction, suffix);
}

static void pc_monitor_create_chart_page(lv_obj_t *parent, pc_monitor_page_t page)
{
    lv_obj_t *page_obj = lv_obj_create(parent);
    if (page_obj == NULL) {
        ESP_LOGE(TAG, "page %u allocation failed", (unsigned)page);
        return;
    }
    lv_obj_remove_style_all(page_obj);
    lv_obj_set_size(page_obj, PC_MONITOR_SCREEN_W, 128);
    lv_obj_set_style_bg_color(page_obj, lv_color_hex(PC_MONITOR_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(page_obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(page_obj, LV_OBJ_FLAG_SCROLLABLE);
    s_pages[page] = page_obj;

    /*
     * 160 x 128 rows below the 18 px title line: the name/value pairs
     * own y=22..38, the chart starts under them. The pair labels are
     * compact status text, so they use the small token (12 px Chinese,
     * 12 px Montserrat fallback for the digits).
     */
    const int32_t row_y = 22;
    const int32_t row_h = 16;
    const int32_t name_x[2] = { 4, 82 };
    const int32_t value_x[2] = { 32, 110 };

    for (size_t i = 0; i < 2; ++i) {
        const char *name = (page == PC_MONITOR_PAGE_CPU_RAM)
                           ? (i == 0 ? "CPU" : "RAM")
                           : (i == 0 ? "GPU" : xiaomiao_text(XM_TEXT_PM_LABEL_TEMP));
        const char *init_val = (page == PC_MONITOR_PAGE_GPU_TEMP && i == 1)
                               ? PC_MONITOR_DEGREE_C : "-- %";
        const uint32_t line_color = (page == PC_MONITOR_PAGE_CPU_RAM)
            ? (i == 0 ? PC_MONITOR_COLOR_CPU : PC_MONITOR_COLOR_RAM)
            : (i == 0 ? PC_MONITOR_COLOR_GPU : PC_MONITOR_COLOR_TEMP);

        s_name_labels[page][i] = pc_monitor_create_label(page_obj, name,
                                                         xiaomiao_font_small(),
                                                         line_color);
        pc_monitor_place(s_name_labels[page][i], name_x[i], row_y, 28, row_h, LV_TEXT_ALIGN_LEFT);

        s_value_labels[page][i] = pc_monitor_create_label(page_obj, init_val,
                                                          xiaomiao_font_small(),
                                                          line_color);
        pc_monitor_place(s_value_labels[page][i], value_x[i], row_y, 48, row_h, LV_TEXT_ALIGN_LEFT);
    }

    const char *y_top = "100";
    lv_obj_t *y_top_label = pc_monitor_create_label(page_obj, y_top,
                                                    xiaomiao_font_small(),
                                                    PC_MONITOR_COLOR_MUTED);
    pc_monitor_place(y_top_label, 0, 42, 18, 12, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *y_bot_label = pc_monitor_create_label(page_obj, "0",
                                                    xiaomiao_font_small(),
                                                    PC_MONITOR_COLOR_MUTED);
    pc_monitor_place(y_bot_label, 0, 88, 18, 12, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *chart = lv_chart_create(page_obj);
    if (chart == NULL) {
        ESP_LOGE(TAG, "chart %u allocation failed", (unsigned)page);
        return;
    }
    lv_obj_set_pos(chart, 20, 42);
    lv_obj_set_size(chart, PC_MONITOR_SCREEN_W - 22, 60);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, PC_MONITOR_HISTORY);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_obj_set_style_bg_color(chart, lv_color_hex(PC_MONITOR_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(PC_MONITOR_COLOR_GRID), 0);
    lv_obj_set_style_pad_all(chart, 3, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(PC_MONITOR_COLOR_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS);

    if (page == PC_MONITOR_PAGE_CPU_RAM) {
        lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        s_series[page][0] = lv_chart_add_series(chart, lv_color_hex(PC_MONITOR_COLOR_CPU),
                                                 LV_CHART_AXIS_PRIMARY_Y);
        s_series[page][1] = lv_chart_add_series(chart, lv_color_hex(PC_MONITOR_COLOR_RAM),
                                                 LV_CHART_AXIS_PRIMARY_Y);
    } else {
        lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        s_series[page][0] = lv_chart_add_series(chart, lv_color_hex(PC_MONITOR_COLOR_GPU),
                                                 LV_CHART_AXIS_PRIMARY_Y);
        s_series[page][1] = lv_chart_add_series(chart, lv_color_hex(PC_MONITOR_COLOR_TEMP),
                                                 LV_CHART_AXIS_PRIMARY_Y);
    }

    for (size_t i = 0; i < 2; ++i) {
        if (s_series[page][i] != NULL) {
            lv_chart_set_all_values(chart, s_series[page][i], LV_CHART_POINT_NONE);
        }
    }
    s_charts[page] = chart;
}

static void pc_monitor_show_page(pc_monitor_page_t page)
{
    for (size_t i = 0; i < PC_MONITOR_PAGE_COUNT; ++i) {
        if (s_pages[i] != NULL) {
            if (i == (size_t)page) {
                lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    s_current_page = page;
    if (s_title_label != NULL) {
        lv_label_set_text(s_title_label, pc_monitor_page_title(page));
    }
}

static void pc_monitor_update_values(const xiaomiao_agent_snapshot_t *snap)
{
    char buf[16];

    if (snap->cpu_valid) {
        pc_monitor_format_value(snap->cpu_percent, " %", buf, sizeof(buf));
    } else {
        strcpy(buf, "-- %");
    }
    if (s_value_labels[PC_MONITOR_PAGE_CPU_RAM][0] != NULL) {
        lv_label_set_text(s_value_labels[PC_MONITOR_PAGE_CPU_RAM][0], buf);
    }

    if (snap->memory_valid) {
        pc_monitor_format_value(snap->memory_percent, " %", buf, sizeof(buf));
    } else {
        strcpy(buf, "-- %");
    }
    if (s_value_labels[PC_MONITOR_PAGE_CPU_RAM][1] != NULL) {
        lv_label_set_text(s_value_labels[PC_MONITOR_PAGE_CPU_RAM][1], buf);
    }

    if (snap->gpu_valid) {
        pc_monitor_format_value(snap->gpu_percent, " %", buf, sizeof(buf));
    } else {
        strcpy(buf, "-- %");
    }
    if (s_value_labels[PC_MONITOR_PAGE_GPU_TEMP][0] != NULL) {
        lv_label_set_text(s_value_labels[PC_MONITOR_PAGE_GPU_TEMP][0], buf);
    }

    /*
     * The temperature row names its source in English ("GPU T"/"CPU T");
     * the Chinese table collapses both to one 24 px CJK word so the row
     * still fits its 28 px label box.
     */
    const char *temp_name = xiaomiao_text(XM_TEXT_PM_LABEL_TEMP);
    const char *temp_val = PC_MONITOR_DEGREE_C;
    if (snap->gpu_temperature_valid) {
        temp_name = xiaomiao_text(XM_TEXT_PM_LABEL_GPU_TEMP);
        pc_monitor_format_value(snap->gpu_temperature_c,
                                " " PC_MONITOR_DEGREE_UTF8 "C", buf, sizeof(buf));
        temp_val = buf;
    } else if (snap->cpu_temperature_valid) {
        temp_name = xiaomiao_text(XM_TEXT_PM_LABEL_CPU_TEMP);
        pc_monitor_format_value(snap->cpu_temperature_c,
                                " " PC_MONITOR_DEGREE_UTF8 "C", buf, sizeof(buf));
        temp_val = buf;
    }
    if (s_name_labels[PC_MONITOR_PAGE_GPU_TEMP][1] != NULL) {
        lv_label_set_text(s_name_labels[PC_MONITOR_PAGE_GPU_TEMP][1], temp_name);
    }
    if (s_value_labels[PC_MONITOR_PAGE_GPU_TEMP][1] != NULL) {
        lv_label_set_text(s_value_labels[PC_MONITOR_PAGE_GPU_TEMP][1], temp_val);
    }
}

static void pc_monitor_push_chart(lv_obj_t *chart, lv_chart_series_t *series,
                                  bool valid, float value, int32_t scale,
                                  int32_t *last_value)
{
    if (chart == NULL || series == NULL) {
        return;
    }
    int32_t point;
    if (valid && isfinite(value)) {
        point = (int32_t)(value + (value >= 0.0f ? 0.5f : -0.5f));
        if (point < 0) {
            point = 0;
        }
        if (point > scale) {
            point = scale;
        }
        *last_value = point;
    } else {
        point = *last_value;
    }
    lv_chart_set_next_value(chart, series, point);
}

static void pc_monitor_update_charts(const xiaomiao_agent_snapshot_t *snap)
{
    pc_monitor_push_chart(s_charts[PC_MONITOR_PAGE_CPU_RAM],
                          s_series[PC_MONITOR_PAGE_CPU_RAM][0],
                          snap->cpu_valid, snap->cpu_percent, 100,
                          &s_last_value[PC_MONITOR_PAGE_CPU_RAM][0]);
    pc_monitor_push_chart(s_charts[PC_MONITOR_PAGE_CPU_RAM],
                          s_series[PC_MONITOR_PAGE_CPU_RAM][1],
                          snap->memory_valid, snap->memory_percent, 100,
                          &s_last_value[PC_MONITOR_PAGE_CPU_RAM][1]);
    pc_monitor_push_chart(s_charts[PC_MONITOR_PAGE_GPU_TEMP],
                          s_series[PC_MONITOR_PAGE_GPU_TEMP][0],
                          snap->gpu_valid, snap->gpu_percent, 100,
                          &s_last_value[PC_MONITOR_PAGE_GPU_TEMP][0]);
    pc_monitor_push_chart(s_charts[PC_MONITOR_PAGE_GPU_TEMP],
                          s_series[PC_MONITOR_PAGE_GPU_TEMP][1],
                          snap->gpu_temperature_valid, snap->gpu_temperature_c, 100,
                          &s_last_value[PC_MONITOR_PAGE_GPU_TEMP][1]);
}

static void pc_monitor_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_b_latched && s_keypad != NULL &&
        lv_indev_get_state(s_keypad) == LV_INDEV_STATE_RELEASED) {
        s_b_latched = false;
    }

    if (s_back_pending) {
        s_back_pending = false;
        s_b_latched = false;
        xiaomiao_navigation_back();
        return;
    }

    if (s_container == NULL) {
        return;
    }

    if (++s_refresh_ticks < PC_MONITOR_REFRESH_TICKS) {
        return;
    }
    s_refresh_ticks = 0;

    xiaomiao_agent_snapshot_t snapshot;
    if (xiaomiao_agent_get_snapshot(&snapshot) != ESP_OK) {
        return;
    }

    pc_monitor_update_values(&snapshot);
    pc_monitor_update_charts(&snapshot);
}

static void pc_monitor_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) {
        return;
    }
    const uint32_t key = lv_event_get_key(event);

    if (key == LV_KEY_RIGHT) {
        pc_monitor_page_t next = (pc_monitor_page_t)((s_current_page + 1) %
                                                      PC_MONITOR_PAGE_COUNT);
        pc_monitor_show_page(next);
    } else if (key == LV_KEY_LEFT) {
        pc_monitor_page_t prev = (pc_monitor_page_t)((s_current_page +
                                                      PC_MONITOR_PAGE_COUNT - 1) %
                                                     PC_MONITOR_PAGE_COUNT);
        pc_monitor_show_page(prev);
    } else if (key == LV_KEY_ESC) {
        if (!s_b_latched) {
            s_b_latched = true;
            s_back_pending = true;
        }
    }
}

static void pc_monitor_open(void)
{
    if (s_container != NULL) {
        ESP_LOGE(TAG, "pc monitor is already open");
        return;
    }

    lv_obj_t *root = xiaomiao_navigation_app_root();
    if (root == NULL) {
        ESP_LOGE(TAG, "no App content root");
        return;
    }

    lv_group_t *group = lv_group_get_default();
    if (group == NULL) {
        ESP_LOGE(TAG, "LVGL default group is not ready");
        return;
    }
    s_keypad = pc_monitor_find_keypad();
    s_group = group;

    lv_obj_t *container = lv_obj_create(root);
    if (container == NULL) {
        ESP_LOGE(TAG, "container allocation failed");
        return;
    }
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, PC_MONITOR_SCREEN_W, 128);
    lv_obj_set_style_bg_color(container, lv_color_hex(PC_MONITOR_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    s_container = container;

    s_title_label = pc_monitor_create_label(container,
                                            pc_monitor_page_title(PC_MONITOR_PAGE_CPU_RAM),
                                            xiaomiao_font_title(),
                                            PC_MONITOR_COLOR_TITLE);
    pc_monitor_place(s_title_label, 0, 2, PC_MONITOR_SCREEN_W, 14,
                     LV_TEXT_ALIGN_CENTER);

    for (size_t p = 0; p < PC_MONITOR_PAGE_COUNT; ++p) {
        pc_monitor_create_chart_page(container, (pc_monitor_page_t)p);
    }
    pc_monitor_show_page(PC_MONITOR_PAGE_CPU_RAM);

    /* Footer sits under the 42..102 chart band and stays one line. */
    s_hint_label = pc_monitor_create_label(container,
                                           xiaomiao_text(XM_TEXT_PM_HINT),
                                           xiaomiao_font_small(),
                                           PC_MONITOR_COLOR_MUTED);
    pc_monitor_place(s_hint_label, 0, 113, PC_MONITOR_SCREEN_W, 14,
                     LV_TEXT_ALIGN_CENTER);

    lv_group_add_obj(s_group, container);
    lv_group_focus_obj(container);
    lv_obj_add_event_cb(container, pc_monitor_key_cb, LV_EVENT_KEY, NULL);

    s_back_pending = false;
    s_b_latched = false;
    s_current_page = PC_MONITOR_PAGE_CPU_RAM;
    s_refresh_ticks = PC_MONITOR_REFRESH_TICKS - 1;
    for (size_t p = 0; p < PC_MONITOR_PAGE_COUNT; ++p) {
        s_last_value[p][0] = LV_CHART_POINT_NONE;
        s_last_value[p][1] = LV_CHART_POINT_NONE;
    }

    s_timer = lv_timer_create(pc_monitor_timer_cb, PC_MONITOR_TIMER_MS, NULL);
    pc_monitor_timer_cb(NULL);

    ESP_LOGI(TAG, "pc monitor opened, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static void pc_monitor_close(void)
{
    if (s_timer != NULL) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    if (s_container != NULL && s_group != NULL) {
        lv_group_remove_obj(s_container);
    }
    s_container = NULL;
    s_title_label = NULL;
    s_hint_label = NULL;
    for (size_t p = 0; p < PC_MONITOR_PAGE_COUNT; ++p) {
        s_pages[p] = NULL;
        s_charts[p] = NULL;
        s_series[p][0] = NULL;
        s_series[p][1] = NULL;
        s_last_value[p][0] = LV_CHART_POINT_NONE;
        s_last_value[p][1] = LV_CHART_POINT_NONE;
        for (size_t i = 0; i < 2; ++i) {
            s_name_labels[p][i] = NULL;
            s_value_labels[p][i] = NULL;
        }
    }
    s_group = NULL;
    s_keypad = NULL;
    s_back_pending = false;
    s_b_latched = false;
    s_refresh_ticks = 0;

    ESP_LOGI(TAG, "pc monitor closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

/*
 * Not const: `name` is the localized text resolved when the Launcher
 * asks for the App, i.e. after the Font Service has latched the
 * language. Registry and Navigation only ever see the pointer returned
 * by the accessor below.
 */
static xiaomiao_app_t s_pc_monitor_app = {
    .id = PC_MONITOR_APP_ID,
    .name = NULL,
    .icon = PC_MONITOR_APP_ICON,
    .init = NULL,
    .open = pc_monitor_open,
    .close = pc_monitor_close,
};

const xiaomiao_app_t *xiaomiao_pc_monitor_app(void)
{
    s_pc_monitor_app.name = xiaomiao_text(XM_TEXT_APP_PC_MONITOR);
    return &s_pc_monitor_app;
}
