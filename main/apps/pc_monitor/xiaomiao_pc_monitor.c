/*
 * PC Monitor App (goal nodes 6, 11 and 12; AI quota pages goal 2026-09-27).
 *
 * Four pages, each with live data:
 *   page 0 - CPU + RAM percent (shared 0..100 scale) with a 60-point chart
 *   page 1 - GPU percent + temperature, same chart layout
 *   page 2 - Zhipu quota: Token 5H and 1W windows
 *   page 3 - Codex quota: 5H and 7D windows
 * RIGHT/LEFT cycle 0->1->2->3->0 and back, B returns to the Launcher. The
 * App joins the LVGL default group on open (like the Settings App) so it
 * receives key events directly; the Launcher root keeps no focus while an
 * App is open.
 *
 * Node 11 feeds the Agent Service snapshot into both the value labels and
 * the chart series. Missing data keeps the node 6 look ("--") and pushes
 * LV_CHART_POINT_NONE so the line has a gap instead of a false zero. The
 * 3-second validity rule lives in the Service, not here.
 *
 * A quota page shows, per window, the remaining percentage, a bar, the
 * PC-local reset time and the remaining time in parentheses. A real 0%
 * stays 0%. The countdown is advanced with the device monotonic clock from
 * the seconds the Service recorded at commit time; once it reaches zero
 * the row reads the pending marker instead of an expired countdown, and a
 * window without valid data reads `--`.
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
#include "esp_timer.h"
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
#define PC_MONITOR_COLOR_ZHIPU     0x4FA8FF
#define PC_MONITOR_COLOR_CODEX     0x30D060

#define PC_MONITOR_SCREEN_W   160
#define PC_MONITOR_HISTORY    60
#define PC_MONITOR_TIMER_MS   20
#define PC_MONITOR_REFRESH_TICKS (1000 / PC_MONITOR_TIMER_MS)

#define PC_MONITOR_DEGREE_UTF8 "\xC2\xB0"
/*
 * One 12 px label carries the whole metric row ("CPU 86.9%"), so the unit
 * never needs a separating space and the name can never collide with the
 * value box. The degree sign is two UTF-8 bytes.
 */
#define PC_MONITOR_UNIT_PERCENT "%"
#define PC_MONITOR_UNIT_TEMP    PC_MONITOR_DEGREE_UTF8 "C"
#define PC_MONITOR_PERCENT_NONE "--%"
#define PC_MONITOR_TEMP_NONE    "--" PC_MONITOR_DEGREE_UTF8 "C"

typedef enum {
    PC_MONITOR_PAGE_CPU_RAM = 0,
    PC_MONITOR_PAGE_GPU_TEMP,
    PC_MONITOR_PAGE_ZHIPU,
    PC_MONITOR_PAGE_CODEX,
    PC_MONITOR_PAGE_COUNT,
} pc_monitor_page_t;

/* The two quota pages, in page order; index with page - PAGE_ZHIPU. */
#define PC_MONITOR_QUOTA_PAGES 2

/*
 * One quota window row: label and percentage share the first line with
 * the bar, the reset time and the countdown share the second.
 */
typedef struct {
    lv_obj_t *label;
    lv_obj_t *percent;
    lv_obj_t *bar;
    lv_obj_t *reset_time;
    lv_obj_t *reset_gap;
} pc_monitor_quota_row_t;

static lv_obj_t *s_container;
static lv_obj_t *s_pages[PC_MONITOR_PAGE_COUNT];
static lv_obj_t *s_title_label;
static lv_obj_t *s_metric_labels[PC_MONITOR_PAGE_COUNT][2];
static lv_obj_t *s_charts[PC_MONITOR_PAGE_COUNT];
static lv_chart_series_t *s_series[PC_MONITOR_PAGE_COUNT][2];
static int32_t s_last_value[PC_MONITOR_PAGE_COUNT][2];
static lv_obj_t *s_state_labels[PC_MONITOR_QUOTA_PAGES];
static pc_monitor_quota_row_t s_quota_rows[PC_MONITOR_QUOTA_PAGES][XIAOMIAO_QUOTA_WINDOWS];
static lv_obj_t *s_page_counter_label;
static lv_obj_t *s_hint_label;
static lv_timer_t *s_timer;
static lv_group_t *s_group;
static lv_indev_t *s_keypad;
static pc_monitor_page_t s_current_page;
static bool s_back_pending;
static bool s_b_latched;
static size_t s_refresh_ticks;

static bool pc_monitor_is_quota_page(pc_monitor_page_t page)
{
    return page == PC_MONITOR_PAGE_ZHIPU || page == PC_MONITOR_PAGE_CODEX;
}

static size_t pc_monitor_quota_index(pc_monitor_page_t page)
{
    return (size_t)(page - PC_MONITOR_PAGE_ZHIPU);
}

static bool pc_monitor_page_provider(pc_monitor_page_t page,
                                     xiaomiao_quota_provider_t *out_provider)
{
    if (page == PC_MONITOR_PAGE_ZHIPU) {
        *out_provider = XIAOMIAO_QUOTA_PROVIDER_ZHIPU;
        return true;
    }
    if (page == PC_MONITOR_PAGE_CODEX) {
        *out_provider = XIAOMIAO_QUOTA_PROVIDER_CODEX;
        return true;
    }
    return false;
}

/*
 * The state words are the frozen display mapping of the Service state:
 * OK, LOGIN (credentials missing or not signed in), SRC (upstream
 * unreachable), BAD (invalid response), STALE (too old), OFF (no Agent
 * or Wi-Fi link).
 */
static const char *pc_monitor_quota_state_text(xiaomiao_quota_state_t state)
{
    switch (state) {
    case XIAOMIAO_QUOTA_OK:
        return xiaomiao_text(XM_TEXT_QUOTA_STATE_OK);
    case XIAOMIAO_QUOTA_AUTH_REQUIRED:
        return xiaomiao_text(XM_TEXT_QUOTA_STATE_LOGIN);
    case XIAOMIAO_QUOTA_UNAVAILABLE:
        return xiaomiao_text(XM_TEXT_QUOTA_STATE_SRC);
    case XIAOMIAO_QUOTA_INVALID_DATA:
        return xiaomiao_text(XM_TEXT_QUOTA_STATE_BAD);
    case XIAOMIAO_QUOTA_STALE:
        return xiaomiao_text(XM_TEXT_QUOTA_STATE_STALE);
    case XIAOMIAO_QUOTA_OFFLINE:
    default:
        return xiaomiao_text(XM_TEXT_QUOTA_STATE_OFF);
    }
}

/*
 * Remaining time in microseconds, or -1 when the window carries no valid
 * countdown. The Service recorded the seconds at commit time, so the
 * elapsed monotonic time since then is simply subtracted.
 */
static int64_t pc_monitor_window_remaining_us(const xiaomiao_quota_window_t *window,
                                              int64_t received_us, int64_t now_us)
{
    if (!window->reset_countdown_valid) {
        return -1;
    }
    int64_t remaining_us = window->reset_in_sec * 1000000;
    if (received_us > 0 && now_us > received_us) {
        remaining_us -= now_us - received_us;
    }
    return remaining_us > 0 ? remaining_us : 0;
}

/*
 * Compact parenthesized remaining time: Nm below an hour, NhNm below a
 * day, NdNh above. Seconds are rounded up into the displayed unit, so a
 * few seconds left never reads as "0m"; a reached zero shows the pending
 * marker instead of an expired countdown.
 */
static void pc_monitor_format_gap(int64_t remaining_us, char *out, size_t size)
{
    if (remaining_us < 0) {
        snprintf(out, size, "--");
        return;
    }
    if (remaining_us == 0) {
        snprintf(out, size, "%s", xiaomiao_text(XM_TEXT_PM_QUOTA_PENDING));
        return;
    }
    const int64_t seconds = (remaining_us + 999999) / 1000000;
    const int total_minutes = (int)((seconds + 59) / 60);
    if (total_minutes < 60) {
        snprintf(out, size, "(%dm)", total_minutes);
    } else if (total_minutes < 24 * 60) {
        snprintf(out, size, "(%dh%dm)", total_minutes / 60, total_minutes % 60);
    } else {
        const int total_hours = (total_minutes + 59) / 60;
        snprintf(out, size, "(%dd%dh)", total_hours / 24, total_hours % 24);
    }
}

static const char *pc_monitor_page_title(pc_monitor_page_t page)
{
    /*
     * One ID per page title. The chart titles are technical labels and
     * stay identical in both languages; the provider titles are frozen by
     * the quota design freezes both provider names).
     */
    static const xiaomiao_text_id_t titles[PC_MONITOR_PAGE_COUNT] = {
        [PC_MONITOR_PAGE_CPU_RAM] = XM_TEXT_PM_TITLE_CPU_RAM,
        [PC_MONITOR_PAGE_GPU_TEMP] = XM_TEXT_PM_TITLE_GPU_TEMP,
        [PC_MONITOR_PAGE_ZHIPU] = XM_TEXT_PM_TITLE_ZHIPU,
        [PC_MONITOR_PAGE_CODEX] = XM_TEXT_PM_TITLE_CODEX,
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

/*
 * Fixed-size labels must never wrap: a wrapped second line would be
 * clipped by the row height and read as a missing glyph.
 */
static void pc_monitor_clip_label(lv_obj_t *label)
{
    if (label != NULL) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    }
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
     * 160 x 128 rows below the 18 px title line: one label per metric owns
     * y=22..38 and carries name, number and unit together ("CPU 86.9%").
     * A single label per column cannot collide with a second box, and the
     * 78 px width leaves room for the widest real string ("RAM 100.0%").
     * The chart starts under the rows.
     */
    const int32_t row_y = 22;
    const int32_t row_h = 16;
    const int32_t metric_x[2] = { 1, 80 };
    const int32_t metric_w = 78;
    const int32_t y_axis_label_width = 24;
    const int32_t chart_x = y_axis_label_width + 2;
    const int32_t chart_right_margin = 2;

    for (size_t i = 0; i < 2; ++i) {
        const char *name = (page == PC_MONITOR_PAGE_CPU_RAM)
                           ? (i == 0 ? "CPU" : "RAM")
                           : (i == 0 ? "GPU" : xiaomiao_text(XM_TEXT_PM_LABEL_TEMP));
        const char *init_val = (page == PC_MONITOR_PAGE_GPU_TEMP && i == 1)
                               ? PC_MONITOR_TEMP_NONE : PC_MONITOR_PERCENT_NONE;
        const uint32_t line_color = (page == PC_MONITOR_PAGE_CPU_RAM)
            ? (i == 0 ? PC_MONITOR_COLOR_CPU : PC_MONITOR_COLOR_RAM)
            : (i == 0 ? PC_MONITOR_COLOR_GPU : PC_MONITOR_COLOR_TEMP);

        char init_text[24];
        snprintf(init_text, sizeof(init_text), "%s %s", name, init_val);
        s_metric_labels[page][i] = pc_monitor_create_label(page_obj, init_text,
                                                           xiaomiao_font_small(),
                                                           line_color);
        pc_monitor_place(s_metric_labels[page][i], metric_x[i], row_y, metric_w,
                         row_h, LV_TEXT_ALIGN_LEFT);
        pc_monitor_clip_label(s_metric_labels[page][i]);
    }

    const char *y_top = "100";
    lv_obj_t *y_top_label = pc_monitor_create_label(page_obj, y_top,
                                                    xiaomiao_font_small(),
                                                    PC_MONITOR_COLOR_MUTED);
    pc_monitor_place(y_top_label, 0, 42, y_axis_label_width, 14, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *y_bot_label = pc_monitor_create_label(page_obj, "0",
                                                    xiaomiao_font_small(),
                                                    PC_MONITOR_COLOR_MUTED);
    pc_monitor_place(y_bot_label, 0, 88, y_axis_label_width, 14, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *chart = lv_chart_create(page_obj);
    if (chart == NULL) {
        ESP_LOGE(TAG, "chart %u allocation failed", (unsigned)page);
        return;
    }
    lv_obj_set_pos(chart, chart_x, 42);
    lv_obj_set_size(chart,
                    PC_MONITOR_SCREEN_W - chart_x - chart_right_margin, 60);
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

/*
 * One quota page: two window rows under the shared title row. Row 1 holds
 * "5H", its percentage and the bar (y 22..36); row 2 holds the reset time
 * and the countdown (y 38..50). The second window repeats the pair at
 * y 60..88. The footer row stays free for the page counter.
 */
static void pc_monitor_create_quota_page(lv_obj_t *parent, pc_monitor_page_t page)
{
    lv_obj_t *page_obj = lv_obj_create(parent);
    if (page_obj == NULL) {
        ESP_LOGE(TAG, "quota page %u allocation failed", (unsigned)page);
        return;
    }
    lv_obj_remove_style_all(page_obj);
    lv_obj_set_size(page_obj, PC_MONITOR_SCREEN_W, 128);
    lv_obj_set_style_bg_color(page_obj, lv_color_hex(PC_MONITOR_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(page_obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(page_obj, LV_OBJ_FLAG_SCROLLABLE);
    s_pages[page] = page_obj;

    const size_t quota_index = pc_monitor_quota_index(page);
    const uint32_t accent = (page == PC_MONITOR_PAGE_ZHIPU) ? PC_MONITOR_COLOR_ZHIPU
                                                           : PC_MONITOR_COLOR_CODEX;
    const int32_t main_y[XIAOMIAO_QUOTA_WINDOWS] = { 24, 66 };
    const int32_t reset_y[XIAOMIAO_QUOTA_WINDOWS] = { 40, 82 };

    for (size_t i = 0; i < XIAOMIAO_QUOTA_WINDOWS; ++i) {
        pc_monitor_quota_row_t *row = &s_quota_rows[quota_index][i];

        row->label = pc_monitor_create_label(page_obj, "--", xiaomiao_font_small(),
                                             PC_MONITOR_COLOR_LABEL);
        pc_monitor_place(row->label, 4, main_y[i], 26, 14, LV_TEXT_ALIGN_LEFT);

        row->percent = pc_monitor_create_label(page_obj, "--", xiaomiao_font_small(),
                                               PC_MONITOR_COLOR_VALUE);
        pc_monitor_place(row->percent, 32, main_y[i], 40, 14, LV_TEXT_ALIGN_LEFT);

        row->bar = lv_bar_create(page_obj);
        if (row->bar != NULL) {
            lv_obj_remove_style_all(row->bar);
            lv_obj_set_pos(row->bar, 74, main_y[i] + 2);
            lv_obj_set_size(row->bar, 82, 10);
            lv_obj_set_style_bg_color(row->bar, lv_color_hex(PC_MONITOR_COLOR_GRID),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row->bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(row->bar, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_color(row->bar, lv_color_hex(accent), LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(row->bar, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_radius(row->bar, 2, LV_PART_INDICATOR);
            lv_bar_set_range(row->bar, 0, 100);
            lv_bar_set_value(row->bar, 0, LV_ANIM_OFF);
        }

        row->reset_time = pc_monitor_create_label(page_obj, "--", xiaomiao_font_small(),
                                                  PC_MONITOR_COLOR_LABEL);
        pc_monitor_place(row->reset_time, 4, reset_y[i], 92, 14, LV_TEXT_ALIGN_LEFT);

        row->reset_gap = pc_monitor_create_label(page_obj, "--", xiaomiao_font_small(),
                                                 PC_MONITOR_COLOR_MUTED);
        pc_monitor_place(row->reset_gap, 98, reset_y[i], 58, 14, LV_TEXT_ALIGN_RIGHT);

        pc_monitor_clip_label(row->label);
        pc_monitor_clip_label(row->percent);
        pc_monitor_clip_label(row->reset_time);
        pc_monitor_clip_label(row->reset_gap);
    }
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

    const bool quota_page = pc_monitor_is_quota_page(page);
    if (s_title_label != NULL) {
        lv_label_set_text(s_title_label, pc_monitor_page_title(page));
        /* Quota pages put the provider state on the right of the title
         * row, so their title is left aligned to leave that room. The
         * title box must hold the 16 px font's 19 px line height - a
         * 14 px box wrapped and clipped the glyphs (device, 2026-09-27). */
        if (quota_page) {
            pc_monitor_place(s_title_label, 4, 1, 64, 20, LV_TEXT_ALIGN_LEFT);
        } else {
            pc_monitor_place(s_title_label, 0, 1, PC_MONITOR_SCREEN_W, 20,
                             LV_TEXT_ALIGN_CENTER);
        }
    }

    for (size_t i = 0; i < PC_MONITOR_QUOTA_PAGES; ++i) {
        if (s_state_labels[i] == NULL) {
            continue;
        }
        if ((size_t)(i + PC_MONITOR_PAGE_ZHIPU) == (size_t)page) {
            lv_obj_clear_flag(s_state_labels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_state_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_hint_label != NULL) {
        lv_label_set_text(s_hint_label,
                          quota_page ? xiaomiao_text(XM_TEXT_PM_QUOTA_HINT)
                                     : xiaomiao_text(XM_TEXT_PM_HINT));
        pc_monitor_place(s_hint_label, 0, 113, quota_page ? 118 : PC_MONITOR_SCREEN_W, 14,
                         LV_TEXT_ALIGN_CENTER);
    }
    if (s_page_counter_label != NULL) {
        if (quota_page) {
            char counter[8];
            snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(page + 1),
                     (unsigned)PC_MONITOR_PAGE_COUNT);
            lv_label_set_text(s_page_counter_label, counter);
            lv_obj_clear_flag(s_page_counter_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_page_counter_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void pc_monitor_set_metric(pc_monitor_page_t page, size_t index,
                                  const char *name, const char *value)
{
    lv_obj_t *label = s_metric_labels[page][index];
    if (label == NULL || name == NULL || value == NULL) {
        return;
    }
    char text[32];
    snprintf(text, sizeof(text), "%s %s", name, value);
    lv_label_set_text(label, text);
}

static void pc_monitor_update_values(const xiaomiao_agent_snapshot_t *snap)
{
    char buf[16];

    if (snap->cpu_valid) {
        pc_monitor_format_value(snap->cpu_percent, PC_MONITOR_UNIT_PERCENT, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", PC_MONITOR_PERCENT_NONE);
    }
    pc_monitor_set_metric(PC_MONITOR_PAGE_CPU_RAM, 0, "CPU", buf);

    if (snap->memory_valid) {
        pc_monitor_format_value(snap->memory_percent, PC_MONITOR_UNIT_PERCENT, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", PC_MONITOR_PERCENT_NONE);
    }
    pc_monitor_set_metric(PC_MONITOR_PAGE_CPU_RAM, 1, "RAM", buf);

    if (snap->gpu_valid) {
        pc_monitor_format_value(snap->gpu_percent, PC_MONITOR_UNIT_PERCENT, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", PC_MONITOR_PERCENT_NONE);
    }
    pc_monitor_set_metric(PC_MONITOR_PAGE_GPU_TEMP, 0, "GPU", buf);

    /*
     * The temperature row names its source in English ("GPU"/"CPU") and
     * collapses to one 24 px CJK word in Chinese; the degree-C unit
     * carries the "temperature" meaning, so the row needs no second label.
     */
    const char *temp_name = xiaomiao_text(XM_TEXT_PM_LABEL_TEMP);
    if (snap->gpu_temperature_valid) {
        temp_name = xiaomiao_text(XM_TEXT_PM_LABEL_GPU_TEMP);
        pc_monitor_format_value(snap->gpu_temperature_c, PC_MONITOR_UNIT_TEMP,
                                buf, sizeof(buf));
    } else if (snap->cpu_temperature_valid) {
        temp_name = xiaomiao_text(XM_TEXT_PM_LABEL_CPU_TEMP);
        pc_monitor_format_value(snap->cpu_temperature_c, PC_MONITOR_UNIT_TEMP,
                                buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", PC_MONITOR_TEMP_NONE);
    }
    pc_monitor_set_metric(PC_MONITOR_PAGE_GPU_TEMP, 1, temp_name, buf);
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

/*
 * Refresh one quota page from the Service snapshot. A missing window, a
 * missing value or a lost countdown each degrade to `--`; a real 0% stays
 * a number and a reached countdown reads as pending.
 */
static void pc_monitor_update_quota(pc_monitor_page_t page, int64_t now_us)
{
    xiaomiao_quota_provider_t provider;
    if (!pc_monitor_page_provider(page, &provider)) {
        return;
    }

    xiaomiao_quota_snapshot_t snapshot;
    if (xiaomiao_agent_get_quota_snapshot(provider, &snapshot) != ESP_OK) {
        return;
    }

    const size_t quota_index = pc_monitor_quota_index(page);
    if (s_state_labels[quota_index] != NULL) {
        lv_label_set_text(s_state_labels[quota_index],
                          pc_monitor_quota_state_text(snapshot.state));
    }

    char buf[24];
    for (size_t i = 0; i < XIAOMIAO_QUOTA_WINDOWS; ++i) {
        if (i >= (size_t)snapshot.window_count) {
            break;
        }
        pc_monitor_quota_row_t *row = &s_quota_rows[quota_index][i];
        const xiaomiao_quota_window_t *window = &snapshot.windows[i];

        if (row->label != NULL && window->label[0] != '\0') {
            lv_label_set_text(row->label, window->label);
        }
        if (row->percent != NULL) {
            if (window->present && window->remaining_valid) {
                snprintf(buf, sizeof(buf), "%u%%",
                         (unsigned)window->remaining_percent);
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            lv_label_set_text(row->percent, buf);
        }
        if (row->bar != NULL) {
            const int value = (window->present && window->remaining_valid)
                                  ? (int)window->remaining_percent
                                  : 0;
            lv_bar_set_value(row->bar, value, LV_ANIM_OFF);
        }
        if (row->reset_time != NULL) {
            if (window->present && window->reset_time_valid) {
                snprintf(buf, sizeof(buf), "R %s", window->reset_at_local);
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            lv_label_set_text(row->reset_time, buf);
        }
        if (row->reset_gap != NULL) {
            if (window->present) {
                pc_monitor_format_gap(
                    pc_monitor_window_remaining_us(window, snapshot.received_us, now_us),
                    buf, sizeof(buf));
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            lv_label_set_text(row->reset_gap, buf);
        }
    }
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
    if (xiaomiao_agent_get_snapshot(&snapshot) == ESP_OK) {
        pc_monitor_update_values(&snapshot);
        pc_monitor_update_charts(&snapshot);
    }

    /*
     * Only the visible quota page is refreshed, so the label churn and the
     * countdown stay bounded to what the user can actually see.
     */
    if (pc_monitor_is_quota_page(s_current_page)) {
        pc_monitor_update_quota(s_current_page, esp_timer_get_time());
    }
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

    for (size_t p = 0; p < PC_MONITOR_PAGE_COUNT; ++p) {
        const pc_monitor_page_t page = (pc_monitor_page_t)p;
        if (pc_monitor_is_quota_page(page)) {
            pc_monitor_create_quota_page(container, page);
        } else {
            pc_monitor_create_chart_page(container, page);
        }
    }

    /*
     * Title, provider states, footer and page counter are created after
     * the pages: the pages are opaque and full screen, so anything that
     * must stay visible has to draw above them.
     */
    s_title_label = pc_monitor_create_label(container,
                                            pc_monitor_page_title(PC_MONITOR_PAGE_CPU_RAM),
                                            xiaomiao_font_title(),
                                            PC_MONITOR_COLOR_TITLE);
    pc_monitor_place(s_title_label, 0, 1, PC_MONITOR_SCREEN_W, 20,
                     LV_TEXT_ALIGN_CENTER);
    pc_monitor_clip_label(s_title_label);

    for (size_t i = 0; i < PC_MONITOR_QUOTA_PAGES; ++i) {
        s_state_labels[i] = pc_monitor_create_label(
            container, xiaomiao_text(XM_TEXT_QUOTA_STATE_OFF),
            xiaomiao_font_small(), PC_MONITOR_COLOR_VALUE);
        /* Ends at x=136: the global Wi-Fi indicator owns 140..158 on the
         * top layer and must never sit on the state text. */
        pc_monitor_place(s_state_labels[i], 82, 2, 54, 16, LV_TEXT_ALIGN_RIGHT);
        pc_monitor_clip_label(s_state_labels[i]);
    }

    /* Footer sits under the 42..102 chart band and stays one line. */
    s_hint_label = pc_monitor_create_label(container,
                                           xiaomiao_text(XM_TEXT_PM_HINT),
                                           xiaomiao_font_small(),
                                           PC_MONITOR_COLOR_MUTED);
    pc_monitor_place(s_hint_label, 0, 113, PC_MONITOR_SCREEN_W, 14,
                     LV_TEXT_ALIGN_CENTER);

    /* Page counter on the footer row of the quota pages only, so the two
     * chart pages keep their previous footer unchanged. */
    s_page_counter_label = pc_monitor_create_label(container, "1/1",
                                                   xiaomiao_font_small(),
                                                   PC_MONITOR_COLOR_MUTED);
    pc_monitor_place(s_page_counter_label, 120, 113, 36, 14, LV_TEXT_ALIGN_RIGHT);
    pc_monitor_clip_label(s_page_counter_label);

    pc_monitor_show_page(PC_MONITOR_PAGE_CPU_RAM);

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
    s_page_counter_label = NULL;
    for (size_t i = 0; i < PC_MONITOR_QUOTA_PAGES; ++i) {
        s_state_labels[i] = NULL;
        for (size_t w = 0; w < XIAOMIAO_QUOTA_WINDOWS; ++w) {
            s_quota_rows[i][w].label = NULL;
            s_quota_rows[i][w].percent = NULL;
            s_quota_rows[i][w].bar = NULL;
            s_quota_rows[i][w].reset_time = NULL;
            s_quota_rows[i][w].reset_gap = NULL;
        }
    }
    for (size_t p = 0; p < PC_MONITOR_PAGE_COUNT; ++p) {
        s_pages[p] = NULL;
        s_charts[p] = NULL;
        s_series[p][0] = NULL;
        s_series[p][1] = NULL;
        s_last_value[p][0] = LV_CHART_POINT_NONE;
        s_last_value[p][1] = LV_CHART_POINT_NONE;
        for (size_t i = 0; i < 2; ++i) {
            s_metric_labels[p][i] = NULL;
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
