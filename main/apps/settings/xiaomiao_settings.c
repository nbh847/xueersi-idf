/*
 * Settings App (goal nodes 8, 9, 10 and 15C).
 *
 * UI skeleton plus the real Service reads: an in-App menu (Wi-Fi /
 * Display / Sound / System) with four detail pages. Everything is built
 * under the Navigation content root; the App never creates, switches or
 * deletes a global screen and never touches hardware or NVS.
 *
 * Boundary rules:
 * - The Wi-Fi page is the configuration and management entry: it reports
 *   the real connection state, persists the automatic-connect
 *   preference through the Settings Service before asking the Wi-Fi
 *   Service to apply it, starts the provisioning session and forgets the
 *   saved network after a confirmation. It never calls `esp_wifi_*`
 *   itself and never sees a password (goal node 10, checkpoint 4).
 * - Sound still names the node that will deliver it: the preference has
 *   no effect until node 12 (goal node 9, decision 3).
 * - Display states a hardware fact instead of a missing setting: the
 *   backlight is tied to VCC, so there is no brightness to store (goal
 *   node 9, decision 4).
 * - System reports the real persistence state, so a degraded boot can
 *   never be shown as "saved" (goal node 9, decisions 5 and 6).
 * - Nothing reads hardware to fabricate a state.
 * - Every user-visible string goes through the i18n layer and every label
 *   through a font token, so a Chinese UI is only a table switch. Dynamic
 *   values stay single-line with the DOTS long mode, which keeps an over
 *   long SSID or phrase inside its column (goal node 15C).
 *
 * Input: the App takes the focus on its own root inside the LVGL default
 * group, so LVGL sends it the key events and the Launcher's key handler
 * stays unreachable while Settings is open.
 *
 * B has two meanings in one physical key. LVGL dispatches LV_KEY_ESC on
 * the press edge and then again every long_press_repeat_time while B is
 * held, and never sends a release for it, so a press is latched
 * (s_b_latched) and the repeats are ignored. A 20 ms LVGL timer clears
 * the latch once the keypad reports the key released (goal decision 10).
 * Closing the App is deferred to that timer as well, so the focused
 * object is never deleted from inside its own key callback. The same
 * timer throttles the one-second refresh of the provisioning page, so no
 * second timer is needed.
 */

#include "xiaomiao_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "framework/xiaomiao_fonts.h"
#include "framework/xiaomiao_i18n.h"
#include "framework/xiaomiao_navigation.h"
#include "services/xiaomiao_settings_service.h"
#include "services/xiaomiao_wifi_service.h"

static const char TAG[] = "settings";

#define SETTINGS_APP_ID    "settings"
/* An edit/pencil glyph reads as "configuration" and does not clash with
 * the Hardware Test icon (LV_SYMBOL_SETTINGS); the Launcher renders
 * icons with the small font token, which carries LV_SYMBOL_EDIT (goal
 * decision 3). */
#define SETTINGS_APP_ICON  LV_SYMBOL_EDIT

/* Same palette as the Launcher, PC Monitor and Tools. */
#define SETTINGS_COLOR_SCREEN_BG      0x0E1016
#define SETTINGS_COLOR_TITLE          0xC8D0E0
#define SETTINGS_COLOR_TEXT           0x9AA6BC
#define SETTINGS_COLOR_MUTED          0x5A6478
#define SETTINGS_COLOR_ROW_BG         0x1B1F2A
#define SETTINGS_COLOR_ROW_BORDER     0x39404F
#define SETTINGS_COLOR_FOCUS_BG       0x2D6CDF
#define SETTINGS_COLOR_FOCUS_BORDER   0xFFFFFF
#define SETTINGS_COLOR_WARN           0xE0A030

/*
 * 160 x 128 layout. Title on top, footer hint at the bottom, four menu
 * rows in between. The small font token is 15 px tall with Montserrat and
 * 14 px with the Chinese font, so 20 px rows with a 22 px step hold the
 * text without clipping; the last row ends at y=106, leaving the footer
 * at y=110 clear (goal decisions 8 and 15C).
 */
#define SETTINGS_SCREEN_W   160
#define SETTINGS_TITLE_Y    2
#define SETTINGS_TITLE_H    16
#define SETTINGS_FOOTER_Y   110
#define SETTINGS_FOOTER_H   14

#define SETTINGS_MENU_X     6
#define SETTINGS_MENU_W     148
#define SETTINGS_MENU_Y0    20
#define SETTINGS_MENU_STEP  22
#define SETTINGS_MENU_H     20

/* Detail pages reuse the Tools grid: page subject, the state it is
 * really in, then one line of context. */
#define SETTINGS_STATUS_HEAD_Y    34
#define SETTINGS_STATUS_STATE_Y   56
#define SETTINGS_STATUS_DETAIL_Y  80
#define SETTINGS_STATUS_LINE_H    16
#define SETTINGS_STATUS_DETAIL_H  14

/* Longest System detail line, e.g. "NVS error 0x110C"; UTF-8 Chinese is
 * three bytes per glyph, so the budget is wider than the pixel one. */
#define SETTINGS_SYSTEM_DETAIL_MAX 40

/*
 * Wi-Fi page: one read-only status line plus three actionable rows, a
 * message line and the footer. Both columns use the small font token, the
 * largest one whose localized pairs still fit a 160 px line
 * ("Forget network" / "Auth failed" and their Chinese equivalents). Rows
 * are 18 px high on a 20 px step, which keeps the last row clear of the
 * message line (goal node 10, checkpoint 4).
 */
#define SETTINGS_WIFI_ROW_X      4
#define SETTINGS_WIFI_ROW_W      152
#define SETTINGS_WIFI_ROW_Y0     20
#define SETTINGS_WIFI_ROW_STEP   20
#define SETTINGS_WIFI_ROW_H      18
#define SETTINGS_WIFI_VALUE_X    92
/* The status line's label is short, so its value column starts further
 * left and can hold a full phrase such as "Not connected". */
#define SETTINGS_WIFI_STATUS_VALUE_X 44
#define SETTINGS_WIFI_ROW_RIGHT      152
#define SETTINGS_WIFI_MESSAGE_Y  98
#define SETTINGS_WIFI_MESSAGE_H  12
#define SETTINGS_WIFI_ROW_COUNT  3
#define SETTINGS_WIFI_ROW_AUTO   0
#define SETTINGS_WIFI_ROW_CONFIG 1
#define SETTINGS_WIFI_ROW_FORGET 2
#define SETTINGS_WIFI_MESSAGE_MAX 40

/* Provisioning page: four information lines, a state line, the footer. */
#define SETTINGS_WIFI_PROV_Y0    24
#define SETTINGS_WIFI_PROV_STEP  16
#define SETTINGS_WIFI_PROV_H     14
#define SETTINGS_WIFI_PROV_LINE_MAX 32

/* Poll period for the B release check; the latch itself is event driven. */
#define SETTINGS_B_RELEASE_POLL_MS 20
/* The provisioning page is refreshed once per second through that timer. */
#define SETTINGS_WIFI_REFRESH_TICKS (1000 / SETTINGS_B_RELEASE_POLL_MS)

typedef enum {
    SETTINGS_VIEW_MENU = 0,
    SETTINGS_VIEW_WIFI,
    SETTINGS_VIEW_WIFI_PROVISIONING,
    SETTINGS_VIEW_DISPLAY,
    SETTINGS_VIEW_SOUND,
    SETTINGS_VIEW_SYSTEM,
} settings_view_t;

/* Wi-Fi page sub-state: the row list, or the forget confirmation. */
typedef enum {
    SETTINGS_WIFI_LIST = 0,
    SETTINGS_WIFI_CONFIRM_FORGET,
} settings_wifi_mode_t;

/* Menu order is the focus order: up/down move the index, A opens it.
 * The labels are resolved through the i18n layer at build time. */
static const xiaomiao_text_id_t s_menu_label_ids[] = {
    XM_TEXT_MENU_WIFI,
    XM_TEXT_SETTINGS_DISPLAY,
    XM_TEXT_SETTINGS_SOUND,
    XM_TEXT_SETTINGS_SYSTEM,
};
#define SETTINGS_MENU_ITEM_COUNT \
    (sizeof(s_menu_label_ids) / sizeof(s_menu_label_ids[0]))

#define SETTINGS_MENU_ITEM_WIFI    0
#define SETTINGS_MENU_ITEM_DISPLAY 1
#define SETTINGS_MENU_ITEM_SOUND   2
#define SETTINGS_MENU_ITEM_SYSTEM  3

/* The input root owns the focus; the content container is rebuilt per
 * view and never holds the focus itself (goal decisions 5 and 6). */
static lv_obj_t *s_root;
static lv_obj_t *s_content;
static lv_obj_t *s_menu_items[SETTINGS_MENU_ITEM_COUNT];
static lv_obj_t *s_wifi_rows[SETTINGS_WIFI_ROW_COUNT];
static lv_obj_t *s_wifi_message;
static lv_obj_t *s_prov_line_ssid;
static lv_obj_t *s_prov_line_password;
static lv_obj_t *s_prov_line_url;
static lv_obj_t *s_prov_state;
static lv_group_t *s_group;
static lv_indev_t *s_keypad;
static lv_timer_t *s_b_release_timer;
static settings_view_t s_view = SETTINGS_VIEW_MENU;
static settings_wifi_mode_t s_wifi_mode = SETTINGS_WIFI_LIST;
static size_t s_menu_index;
static size_t s_wifi_index;
static uint32_t s_wifi_refresh_ticks;
static char s_wifi_message_text[SETTINGS_WIFI_MESSAGE_MAX];
static bool s_b_latched;
static bool s_back_pending;

static void settings_show_view(settings_view_t view);
static void settings_handle_escape(void);static lv_obj_t *settings_create_label(lv_obj_t *parent, const char *text,
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
 * the box instead of overflowing it (goal decision 8). */
static lv_obj_t *settings_place_label(lv_obj_t *parent, const char *text,
                                      const lv_font_t *font, uint32_t color,
                                      int32_t x, int32_t y, int32_t width,
                                      int32_t height, lv_text_align_t align)
{
    lv_obj_t *label = settings_create_label(parent, text, font, color,
                                            LV_LABEL_LONG_MODE_DOTS);
    if (label == NULL) {
        return NULL;
    }

    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    return label;
}

static lv_obj_t *settings_create_page_title(lv_obj_t *parent, const char *text)
{
    return settings_place_label(parent, text, xiaomiao_font_small(),
                                SETTINGS_COLOR_TITLE, 0, SETTINGS_TITLE_Y,
                                SETTINGS_SCREEN_W, SETTINGS_TITLE_H,
                                LV_TEXT_ALIGN_CENTER);
}

static void settings_create_footer(lv_obj_t *parent, const char *text)
{
    settings_place_label(parent, text, xiaomiao_font_small(),
                         SETTINGS_COLOR_MUTED, 0, SETTINGS_FOOTER_Y,
                         SETTINGS_SCREEN_W, SETTINGS_FOOTER_H,
                         LV_TEXT_ALIGN_CENTER);
}

static void settings_menu_highlight(void)
{
    if (s_view != SETTINGS_VIEW_MENU) {
        return;
    }

    for (size_t i = 0; i < SETTINGS_MENU_ITEM_COUNT; ++i) {
        lv_obj_t *row = s_menu_items[i];
        if (row == NULL) {
            continue;
        }

        const bool focused = (i == s_menu_index);
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(focused ? SETTINGS_COLOR_FOCUS_BG
                                                       : SETTINGS_COLOR_ROW_BG),
                                  0);
        lv_obj_set_style_border_color(row,
                                      lv_color_hex(focused ? SETTINGS_COLOR_FOCUS_BORDER
                                                           : SETTINGS_COLOR_ROW_BORDER),
                                      0);
        lv_obj_set_style_border_width(row, focused ? 2 : 1, 0);
    }
}

static void settings_build_menu(lv_obj_t *content)
{
    settings_create_page_title(content, xiaomiao_text(XM_TEXT_APP_SETTINGS));

    for (size_t i = 0; i < SETTINGS_MENU_ITEM_COUNT; ++i) {
        const int32_t y = SETTINGS_MENU_Y0 + (int32_t)i * SETTINGS_MENU_STEP;

        lv_obj_t *row = lv_obj_create(content);
        if (row == NULL) {
            ESP_LOGW(TAG, "menu row allocation failed");
            continue;
        }
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, SETTINGS_MENU_X, y);
        lv_obj_set_size(row, SETTINGS_MENU_W, SETTINGS_MENU_H);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        /* Decoration only: the root below stays the single focus object. */
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *label = settings_create_label(row, xiaomiao_text(s_menu_label_ids[i]),
                                                xiaomiao_font_small(),
                                                SETTINGS_COLOR_TITLE,
                                                LV_LABEL_LONG_MODE_DOTS);
        if (label != NULL) {
            lv_obj_set_size(label, SETTINGS_MENU_W - 16, 16);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
        }

        s_menu_items[i] = row;
    }

    settings_menu_highlight();
    settings_create_footer(content, xiaomiao_text(XM_TEXT_HINT_A_OPEN_B_BACK));
}

/*
 * One detail page: the section, the state it is really in, and one line
 * of context. Every page is read-only, so no line can be mistaken for a
 * control or for a capability that is not there (goal node 8 decisions
 * 12 and 13; goal node 9 decision 5).
 */
static void settings_build_detail(lv_obj_t *content, const char *title,
                                  const char *headline, const char *state,
                                  uint32_t state_color, const char *detail)
{
    settings_create_page_title(content, title);

    settings_place_label(content, headline, xiaomiao_font_small(),
                         SETTINGS_COLOR_TEXT, 0, SETTINGS_STATUS_HEAD_Y,
                         SETTINGS_SCREEN_W, SETTINGS_STATUS_LINE_H,
                         LV_TEXT_ALIGN_CENTER);
    settings_place_label(content, state, xiaomiao_font_small(), state_color,
                         0, SETTINGS_STATUS_STATE_Y, SETTINGS_SCREEN_W,
                         SETTINGS_STATUS_LINE_H, LV_TEXT_ALIGN_CENTER);
    settings_place_label(content, detail, xiaomiao_font_small(),
                         SETTINGS_COLOR_MUTED, 0, SETTINGS_STATUS_DETAIL_Y,
                         SETTINGS_SCREEN_W, SETTINGS_STATUS_DETAIL_H,
                         LV_TEXT_ALIGN_CENTER);

    settings_create_footer(content, xiaomiao_text(XM_TEXT_HINT_B_BACK));
}

/*
 * The only page that reads a Service, and only through its public
 * interface. It reports where the current configuration came from and,
 * when there is one, the raw error of the last failed load or write.
 * The state is read once per entry and no timer is created.
 */
static void settings_build_system(lv_obj_t *content)
{
    /* Fetched to prove the Service is readable; the page reports the
     * Service state, not the values of the persisted fields. */
    xiaomiao_settings_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    const esp_err_t get_err = xiaomiao_settings_get(&snapshot);
    (void)snapshot;

    char detail[SETTINGS_SYSTEM_DETAIL_MAX];
    const char *state;
    uint32_t state_color;

    if (get_err != ESP_OK) {
        /* The states must stay distinguishable: a degraded boot is never
         * displayed as a successful save (goal node 9, decision 6). */
        state = xiaomiao_text(XM_TEXT_STATE_UNAVAILABLE);
        state_color = SETTINGS_COLOR_WARN;
        snprintf(detail, sizeof(detail), "%s",
                 xiaomiao_text(XM_TEXT_SETTINGS_SETTINGS_UNAVAILABLE));
    }
    else {
        switch (xiaomiao_settings_source()) {
        case XIAOMIAO_SETTINGS_SOURCE_NVS:
            state = xiaomiao_text(XM_TEXT_SETTINGS_FROM_NVS);
            state_color = SETTINGS_COLOR_TEXT;
            break;
        case XIAOMIAO_SETTINGS_SOURCE_RECOVERED:
            state = xiaomiao_text(XM_TEXT_SETTINGS_RECOVERED);
            state_color = SETTINGS_COLOR_WARN;
            break;
        case XIAOMIAO_SETTINGS_SOURCE_DEGRADED:
            state = xiaomiao_text(XM_TEXT_SETTINGS_NOT_PERSISTED);
            state_color = SETTINGS_COLOR_WARN;
            break;
        case XIAOMIAO_SETTINGS_SOURCE_DEFAULTS:
        default:
            state = xiaomiao_text(XM_TEXT_SETTINGS_DEFAULTS);
            state_color = SETTINGS_COLOR_TEXT;
            break;
        }

        const esp_err_t last_err = xiaomiao_settings_last_error();
        if (last_err != ESP_OK) {
            snprintf(detail, sizeof(detail), "%s 0x%X",
                     xiaomiao_text(XM_TEXT_SETTINGS_NVS_ERROR), (unsigned)last_err);
        }
        else {
            snprintf(detail, sizeof(detail), "%s",
                     xiaomiao_text(XM_TEXT_SETTINGS_STORED_FIELDS));
        }
    }

    settings_build_detail(content, xiaomiao_text(XM_TEXT_SETTINGS_SYSTEM),
                          xiaomiao_text(XM_TEXT_SETTINGS_SERVICE), state,
                          state_color, detail);
}

/*
 * ------------------------------------------------------------------
 * Wi-Fi page (goal node 10, checkpoint 4)
 * ------------------------------------------------------------------
 * Status, automatic connection, configuration and forgetting. The page
 * only ever shows what the Wi-Fi Service reports; when the Service is
 * missing it says so instead of guessing.
 */

static const char *settings_wifi_state_text(const xiaomiao_wifi_snapshot_t *snapshot)
{
    switch (snapshot->state) {
    case XIAOMIAO_WIFI_CONNECTED:
        return xiaomiao_text(XM_TEXT_STATE_CONNECTED);
    case XIAOMIAO_WIFI_CONNECTING:
        return xiaomiao_text(XM_TEXT_STATE_CONNECTING);
    case XIAOMIAO_WIFI_RETRY_WAIT:
        return xiaomiao_text(XM_TEXT_STATE_RECONNECTING);
    case XIAOMIAO_WIFI_SCANNING:
        return xiaomiao_text(XM_TEXT_STATE_SCANNING);
    case XIAOMIAO_WIFI_PROVISIONING:
        return xiaomiao_text(XM_TEXT_STATE_SETUP);
    case XIAOMIAO_WIFI_DISABLED:
        return xiaomiao_text(XM_TEXT_STATE_OFF);
    case XIAOMIAO_WIFI_NO_CREDENTIALS:
        return xiaomiao_text(XM_TEXT_STATE_NOT_CONFIGURED);
    case XIAOMIAO_WIFI_AUTH_FAILED:
        return xiaomiao_text(XM_TEXT_STATE_AUTH_FAILED);
    case XIAOMIAO_WIFI_ERROR:
        return xiaomiao_text(XM_TEXT_STATE_ERROR);
    case XIAOMIAO_WIFI_DISCONNECTED:
    default:
        return xiaomiao_text(XM_TEXT_STATE_NOT_CONNECTED);
    }
}

static uint32_t settings_wifi_state_color(const xiaomiao_wifi_snapshot_t *snapshot)
{
    switch (snapshot->state) {
    case XIAOMIAO_WIFI_CONNECTED:
        return SETTINGS_COLOR_TEXT;
    case XIAOMIAO_WIFI_AUTH_FAILED:
    case XIAOMIAO_WIFI_ERROR:
        return SETTINGS_COLOR_WARN;
    default:
        return SETTINGS_COLOR_MUTED;
    }
}

/* One row: label on the left, optional value on the right, starting at
 * `value_x`. Actionable rows take the focus highlight (goal node 10,
 * checkpoint 4). */
static lv_obj_t *settings_wifi_row(lv_obj_t *content, const char *label, int32_t y,
                                   int32_t value_x, lv_obj_t **out_value)
{
    if (out_value != NULL) {
        *out_value = NULL;
    }

    lv_obj_t *row = lv_obj_create(content);
    if (row == NULL) {
        ESP_LOGW(TAG, "wifi row allocation failed");
        return NULL;
    }
    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, SETTINGS_WIFI_ROW_X, y);
    lv_obj_set_size(row, SETTINGS_WIFI_ROW_W, SETTINGS_WIFI_ROW_H);
    lv_obj_set_style_radius(row, 3, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(SETTINGS_COLOR_ROW_BORDER), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(SETTINGS_COLOR_ROW_BG), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    /* Decoration only: the root below stays the single focus object. */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *name = settings_create_label(row, label, xiaomiao_font_small(),
                                           SETTINGS_COLOR_TITLE, LV_LABEL_LONG_MODE_DOTS);
    if (name != NULL) {
        lv_obj_set_size(name, value_x - 12, 14);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);
    }

    if (out_value != NULL) {
        lv_obj_t *value = settings_create_label(row, "", xiaomiao_font_small(),
                                                SETTINGS_COLOR_TEXT, LV_LABEL_LONG_MODE_DOTS);
        if (value != NULL) {
            lv_obj_set_size(value, SETTINGS_WIFI_ROW_RIGHT - value_x, 14);
            lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(value, LV_ALIGN_RIGHT_MID, -4, 0);
        }
        *out_value = value;
    }

    return row;
}

static void settings_wifi_highlight(void)
{
    if (s_view != SETTINGS_VIEW_WIFI || s_wifi_mode != SETTINGS_WIFI_LIST) {
        return;
    }

    for (size_t i = 0; i < SETTINGS_WIFI_ROW_COUNT; ++i) {
        lv_obj_t *row = s_wifi_rows[i];
        if (row == NULL) {
            continue;
        }

        const bool focused = (i == s_wifi_index);
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(focused ? SETTINGS_COLOR_FOCUS_BG
                                                       : SETTINGS_COLOR_ROW_BG),
                                  0);
        lv_obj_set_style_border_color(row,
                                      lv_color_hex(focused ? SETTINGS_COLOR_FOCUS_BORDER
                                                           : SETTINGS_COLOR_ROW_BORDER),
                                      0);
        lv_obj_set_style_border_width(row, focused ? 2 : 1, 0);
    }
}

static void settings_set_wifi_message(const char *text)
{
    snprintf(s_wifi_message_text, sizeof(s_wifi_message_text), "%s", (text != NULL) ? text : "");
}

static void settings_build_wifi(lv_obj_t *content)
{
    xiaomiao_wifi_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    const esp_err_t snapshot_err = xiaomiao_wifi_get_snapshot(&snapshot);

    settings_create_page_title(content, xiaomiao_text(XM_TEXT_MENU_WIFI));

    lv_obj_t *status_value = NULL;
    (void)settings_wifi_row(content, xiaomiao_text(XM_TEXT_LABEL_STATUS),
                            SETTINGS_WIFI_ROW_Y0, SETTINGS_WIFI_STATUS_VALUE_X,
                            &status_value);
    if (status_value != NULL) {
        const char *text = xiaomiao_text(XM_TEXT_STATE_UNAVAILABLE);
        uint32_t color = SETTINGS_COLOR_WARN;
        if (snapshot_err == ESP_OK) {
            text = settings_wifi_state_text(&snapshot);
            color = settings_wifi_state_color(&snapshot);
        }
        lv_label_set_text(status_value, text);
        lv_obj_set_style_text_color(status_value, lv_color_hex(color), 0);
    }

    lv_obj_t *auto_value = NULL;
    s_wifi_rows[SETTINGS_WIFI_ROW_AUTO] =
        settings_wifi_row(content, xiaomiao_text(XM_TEXT_LABEL_AUTO_CONNECT),
                          SETTINGS_WIFI_ROW_Y0 + SETTINGS_WIFI_ROW_STEP,
                          SETTINGS_WIFI_VALUE_X, &auto_value);
    if (auto_value != NULL) {
        const bool on = (snapshot_err == ESP_OK) && snapshot.auto_connect;
        lv_label_set_text(auto_value,
                          xiaomiao_text(on ? XM_TEXT_STATE_ON : XM_TEXT_STATE_OFF));
        lv_obj_set_style_text_color(auto_value,
                                    lv_color_hex(on ? SETTINGS_COLOR_TEXT : SETTINGS_COLOR_MUTED),
                                    0);
    }

    s_wifi_rows[SETTINGS_WIFI_ROW_CONFIG] =
        settings_wifi_row(content, xiaomiao_text(XM_TEXT_LABEL_CONFIGURE),
                          SETTINGS_WIFI_ROW_Y0 + 2 * SETTINGS_WIFI_ROW_STEP,
                          SETTINGS_WIFI_VALUE_X, NULL);

    const bool confirming = (s_wifi_mode == SETTINGS_WIFI_CONFIRM_FORGET);
    s_wifi_rows[SETTINGS_WIFI_ROW_FORGET] =
        settings_wifi_row(content,
                          xiaomiao_text(confirming ? XM_TEXT_LABEL_FORGET_CONFIRM
                                          : XM_TEXT_LABEL_FORGET),
                          SETTINGS_WIFI_ROW_Y0 + 3 * SETTINGS_WIFI_ROW_STEP,
                          SETTINGS_WIFI_VALUE_X, NULL);

    s_wifi_message = settings_place_label(content, s_wifi_message_text,
                                          xiaomiao_font_small(), SETTINGS_COLOR_WARN, 0,
                                          SETTINGS_WIFI_MESSAGE_Y, SETTINGS_SCREEN_W,
                                          SETTINGS_WIFI_MESSAGE_H, LV_TEXT_ALIGN_CENTER);

    settings_create_footer(content,
                           xiaomiao_text(confirming ? XM_TEXT_HINT_A_YES_B_NO
                                           : XM_TEXT_HINT_A_SELECT_B_BACK));
    settings_wifi_highlight();
}

/*
 * Provisioning page: what the user needs to join the hotspot, plus the
 * live trial result. The temporary password is shown here and nowhere
 * else (goal node 10, "Data lifecycle").
 */
static void settings_build_wifi_provisioning(lv_obj_t *content)
{
    char line[SETTINGS_WIFI_PROV_LINE_MAX];

    settings_create_page_title(content, xiaomiao_text(XM_TEXT_SETTINGS_PROV_TITLE));

    const int32_t y0 = SETTINGS_WIFI_PROV_Y0;

    snprintf(line, sizeof(line), "%s: -", xiaomiao_text(XM_TEXT_LABEL_SSID));
    s_prov_line_ssid = settings_place_label(content, line, xiaomiao_font_small(),
                                            SETTINGS_COLOR_TEXT, 4, y0, SETTINGS_SCREEN_W - 8,
                                            SETTINGS_WIFI_PROV_H, LV_TEXT_ALIGN_LEFT);

    snprintf(line, sizeof(line), "%s: -", xiaomiao_text(XM_TEXT_LABEL_PASSWORD));
    s_prov_line_password = settings_place_label(content, line, xiaomiao_font_small(),
                                                SETTINGS_COLOR_TITLE, 4,
                                                y0 + SETTINGS_WIFI_PROV_STEP,
                                                SETTINGS_SCREEN_W - 8, SETTINGS_WIFI_PROV_H,
                                                LV_TEXT_ALIGN_LEFT);

    snprintf(line, sizeof(line), "%s: %s", xiaomiao_text(XM_TEXT_LABEL_OPEN),
             XIAOMIAO_WIFI_PROVISIONING_URL);
    s_prov_line_url = settings_place_label(content, line, xiaomiao_font_small(),
                                           SETTINGS_COLOR_TEXT, 4,
                                           y0 + 2 * SETTINGS_WIFI_PROV_STEP,
                                           SETTINGS_SCREEN_W - 8, SETTINGS_WIFI_PROV_H,
                                           LV_TEXT_ALIGN_LEFT);

    s_prov_state = settings_place_label(content,
                                        xiaomiao_text(XM_TEXT_SETTINGS_PROV_WAITING),
                                        xiaomiao_font_small(),
                                        SETTINGS_COLOR_WARN, 4, y0 + 3 * SETTINGS_WIFI_PROV_STEP,
                                        SETTINGS_SCREEN_W - 8, SETTINGS_WIFI_PROV_H,
                                        LV_TEXT_ALIGN_LEFT);

    settings_create_footer(content, xiaomiao_text(XM_TEXT_HINT_B_CANCEL));
}

static const char *settings_wifi_setup_text(xiaomiao_wifi_setup_state_t state, esp_err_t error)
{
    switch (state) {
    case XIAOMIAO_WIFI_SETUP_CONNECTING:
        return xiaomiao_text(XM_TEXT_SETTINGS_PROV_CONNECTING);
    case XIAOMIAO_WIFI_SETUP_CONNECTED:
        return xiaomiao_text(XM_TEXT_STATE_CONNECTED);
    case XIAOMIAO_WIFI_SETUP_SAVE_FAILED:
        return xiaomiao_text(XM_TEXT_SETTINGS_PROV_CONNECTED_NOT_SAVED);
    case XIAOMIAO_WIFI_SETUP_FAILED:
        switch (error) {
        case ESP_ERR_INVALID_STATE:
            return xiaomiao_text(XM_TEXT_SETTINGS_PROV_WRONG_PASSWORD);
        case ESP_ERR_NOT_FOUND:
            return xiaomiao_text(XM_TEXT_SETTINGS_PROV_NOT_FOUND);
        case ESP_ERR_TIMEOUT:
            return xiaomiao_text(XM_TEXT_SETTINGS_PROV_TIMEOUT);
        default:
            return xiaomiao_text(XM_TEXT_SETTINGS_PROV_FAILED);
        }
    case XIAOMIAO_WIFI_SETUP_IDLE:
    default:
        return xiaomiao_text(XM_TEXT_SETTINGS_PROV_WAIT_PHONE);
    }
}

static void settings_wifi_prov_refresh(void)
{
    if (s_view != SETTINGS_VIEW_WIFI_PROVISIONING) {
        return;
    }

    xiaomiao_wifi_provisioning_info_t info;
    memset(&info, 0, sizeof(info));
    if (xiaomiao_wifi_provisioning_get_info(&info) != ESP_OK || !info.active) {
        /*
         * The session ended on its own (success or idle timeout). Leave
         * the setup page instead of showing a page that no longer
         * describes anything (goal node 10, checkpoint 4).
         */
        settings_set_wifi_message(xiaomiao_text(
            (info.setup_state == XIAOMIAO_WIFI_SETUP_CONNECTED)
                ? XM_TEXT_SETTINGS_MSG_SETUP_COMPLETE
                : XM_TEXT_SETTINGS_MSG_SETUP_CLOSED));
        s_wifi_mode = SETTINGS_WIFI_LIST;
        settings_show_view(SETTINGS_VIEW_WIFI);
        return;
    }

    char line[SETTINGS_WIFI_PROV_LINE_MAX * 2];
    if (s_prov_line_ssid != NULL) {
        snprintf(line, sizeof(line), "%s: %s", xiaomiao_text(XM_TEXT_LABEL_SSID),
                 info.ssid);
        lv_label_set_text(s_prov_line_ssid, line);
    }
    if (s_prov_line_password != NULL) {
        snprintf(line, sizeof(line), "%s: %s", xiaomiao_text(XM_TEXT_LABEL_PASSWORD),
                 info.password);
        lv_label_set_text(s_prov_line_password, line);
    }
    if (s_prov_state != NULL) {
        const char *state_text = settings_wifi_setup_text(info.setup_state, info.setup_error);
        if (info.setup_state == XIAOMIAO_WIFI_SETUP_IDLE) {
            snprintf(line, sizeof(line), "%s  %us", state_text, (unsigned)info.remaining_s);
            lv_label_set_text(s_prov_state, line);
        }
        else {
            lv_label_set_text(s_prov_state, state_text);
        }
        lv_obj_set_style_text_color(s_prov_state,
                                    lv_color_hex((info.setup_state == XIAOMIAO_WIFI_SETUP_FAILED)
                                                     ? SETTINGS_COLOR_WARN
                                                     : SETTINGS_COLOR_TEXT),
                                    0);
    }
}

static void settings_show_view(settings_view_t view)
{
    if (s_content == NULL) {
        ESP_LOGE(TAG, "no content container for view %d", (int)view);
        return;
    }

    /* Only the Settings content is rebuilt; the input root and the
     * Navigation content root stay untouched (goal decision 6). */
    lv_obj_clean(s_content);
    for (size_t i = 0; i < SETTINGS_MENU_ITEM_COUNT; ++i) {
        s_menu_items[i] = NULL;
    }
    for (size_t i = 0; i < SETTINGS_WIFI_ROW_COUNT; ++i) {
        s_wifi_rows[i] = NULL;
    }
    s_wifi_message = NULL;
    s_prov_line_ssid = NULL;
    s_prov_line_password = NULL;
    s_prov_line_url = NULL;
    s_prov_state = NULL;

    s_view = view;

    switch (view) {
    case SETTINGS_VIEW_MENU:
        settings_build_menu(s_content);
        break;
    case SETTINGS_VIEW_WIFI:
        settings_build_wifi(s_content);
        break;
    case SETTINGS_VIEW_WIFI_PROVISIONING:
        settings_build_wifi_provisioning(s_content);
        settings_wifi_prov_refresh();
        break;
    case SETTINGS_VIEW_DISPLAY:
        /* Hardware fact, not a missing setting: the backlight is wired
         * to VCC, so there is no brightness to store or restore. */
        settings_build_detail(s_content, xiaomiao_text(XM_TEXT_SETTINGS_DISPLAY),
                              xiaomiao_text(XM_TEXT_SETTINGS_BRIGHTNESS_FIXED),
                              xiaomiao_text(XM_TEXT_SETTINGS_BACKLIGHT_VCC),
                              SETTINGS_COLOR_TEXT,
                              xiaomiao_text(XM_TEXT_SETTINGS_NO_DISPLAY));
        break;
    case SETTINGS_VIEW_SOUND:
        settings_build_detail(s_content, xiaomiao_text(XM_TEXT_SETTINGS_SOUND),
                              xiaomiao_text(XM_TEXT_SETTINGS_AUDIO_SERVICE),
                              xiaomiao_text(XM_TEXT_STATE_UNAVAILABLE),
                              SETTINGS_COLOR_WARN,
                              xiaomiao_text(XM_TEXT_SETTINGS_NODE12));
        break;
    case SETTINGS_VIEW_SYSTEM:
        settings_build_system(s_content);
        break;
    }

    ESP_LOGD(TAG, "view=%d", (int)view);
}

static void settings_menu_move(int step)
{
    if (step < 0) {
        if (s_menu_index == 0) {
            return;
        }
        s_menu_index--;
    }
    else {
        if (s_menu_index + 1 >= SETTINGS_MENU_ITEM_COUNT) {
            return;
        }
        s_menu_index++;
    }

    settings_menu_highlight();
}

static void settings_menu_activate(void)
{
    switch (s_menu_index) {
    case SETTINGS_MENU_ITEM_WIFI:
        settings_show_view(SETTINGS_VIEW_WIFI);
        break;
    case SETTINGS_MENU_ITEM_DISPLAY:
        settings_show_view(SETTINGS_VIEW_DISPLAY);
        break;
    case SETTINGS_MENU_ITEM_SOUND:
        settings_show_view(SETTINGS_VIEW_SOUND);
        break;
    case SETTINGS_MENU_ITEM_SYSTEM:
        settings_show_view(SETTINGS_VIEW_SYSTEM);
        break;
    default:
        break;
    }
}

static void settings_wifi_move(int step)
{
    if (step < 0) {
        if (s_wifi_index == 0) {
            return;
        }
        s_wifi_index--;
    }
    else {
        if (s_wifi_index + 1 >= SETTINGS_WIFI_ROW_COUNT) {
            return;
        }
        s_wifi_index++;
    }

    settings_wifi_highlight();
}

/*
 * Persist the preference through the Settings Service first and only
 * then ask the Wi-Fi Service to apply it: a value the UI would show but
 * that never reached Flash is exactly the fake state the goal forbids
 * (goal node 10, "Settings / Wi-Fi").
 */
static void settings_wifi_toggle_auto_connect(void)
{
    xiaomiao_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    if (xiaomiao_settings_get(&settings) != ESP_OK) {
        settings_set_wifi_message(xiaomiao_text(XM_TEXT_SETTINGS_SETTINGS_UNAVAILABLE));
        settings_show_view(SETTINGS_VIEW_WIFI);
        return;
    }

    const bool target = !settings.wifi_auto_connect;
    settings.wifi_auto_connect = target;

    const esp_err_t store_err = xiaomiao_settings_set(&settings);
    if (store_err != ESP_OK) {
        settings_set_wifi_message(xiaomiao_text(XM_TEXT_SETTINGS_MSG_SAVE_FAILED));
        settings_show_view(SETTINGS_VIEW_WIFI);
        return;
    }

    const esp_err_t apply_err = xiaomiao_wifi_apply_auto_connect(target);
    settings_set_wifi_message((apply_err == ESP_OK)
                                  ? ""
                                  : xiaomiao_text(XM_TEXT_SETTINGS_MSG_APPLY_FAILED));
    settings_show_view(SETTINGS_VIEW_WIFI);
}

static void settings_wifi_start_provisioning(void)
{
    const esp_err_t err = xiaomiao_wifi_provisioning_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "provisioning start failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
        settings_set_wifi_message(xiaomiao_text(XM_TEXT_SETTINGS_MSG_SETUP_FAILED));
        settings_show_view(SETTINGS_VIEW_WIFI);
        return;
    }

    settings_set_wifi_message("");
    s_wifi_refresh_ticks = 0;
    settings_show_view(SETTINGS_VIEW_WIFI_PROVISIONING);
}

static void settings_wifi_forget(void)
{
    s_wifi_mode = SETTINGS_WIFI_LIST;

    const esp_err_t err = xiaomiao_wifi_forget_credentials();
    settings_set_wifi_message(xiaomiao_text((err == ESP_OK)
                                                ? XM_TEXT_SETTINGS_MSG_FORGOTTEN
                                                : XM_TEXT_SETTINGS_MSG_FORGET_FAILED));
    settings_show_view(SETTINGS_VIEW_WIFI);
}

static void settings_wifi_activate(void)
{
    switch (s_wifi_index) {
    case SETTINGS_WIFI_ROW_AUTO:
        settings_wifi_toggle_auto_connect();
        break;
    case SETTINGS_WIFI_ROW_CONFIG:
        settings_wifi_start_provisioning();
        break;
    case SETTINGS_WIFI_ROW_FORGET:
        s_wifi_mode = SETTINGS_WIFI_CONFIRM_FORGET;
        settings_set_wifi_message(xiaomiao_text(XM_TEXT_SETTINGS_MSG_PRESS_CONFIRM));
        settings_show_view(SETTINGS_VIEW_WIFI);
        break;
    default:
        break;
    }
}

static void settings_wifi_key(uint32_t key)
{
    if (s_wifi_mode == SETTINGS_WIFI_CONFIRM_FORGET) {
        /* The confirmation only answers to A and B. */
        if (key == LV_KEY_ENTER) {
            settings_wifi_forget();
        }
        else if (key == LV_KEY_ESC) {
            s_wifi_mode = SETTINGS_WIFI_LIST;
            settings_set_wifi_message(xiaomiao_text(XM_TEXT_SETTINGS_MSG_CANCELLED));
            settings_show_view(SETTINGS_VIEW_WIFI);
        }
        return;
    }

    switch (key) {
    case LV_KEY_UP:
        settings_wifi_move(-1);
        break;
    case LV_KEY_DOWN:
        settings_wifi_move(1);
        break;
    case LV_KEY_ENTER:
        settings_wifi_activate();
        break;
    case LV_KEY_ESC:
        settings_handle_escape();
        break;
    default:
        break;
    }
}

/*
 * One physical B press moves exactly one level. The first ESC event of a
 * press latches, every repeat LVGL sends while B is held is dropped, and
 * the timer clears the latch once the key is released (goal decision 10).
 */
static void settings_handle_escape(void)
{
    if (s_b_latched) {
        return;
    }
    s_b_latched = true;

    if (s_view == SETTINGS_VIEW_WIFI_PROVISIONING) {
        /* B is the documented way out of a provisioning session. */
        const esp_err_t err = xiaomiao_wifi_provisioning_stop();
        settings_set_wifi_message(xiaomiao_text((err == ESP_OK)
                                                    ? XM_TEXT_SETTINGS_MSG_SETUP_CANCELLED
                                                    : XM_TEXT_SETTINGS_MSG_SETUP_STOP_FAILED));
        settings_show_view(SETTINGS_VIEW_WIFI);
        return;
    }

    if (s_view != SETTINGS_VIEW_MENU) {
        settings_show_view(SETTINGS_VIEW_MENU);
        return;
    }

    /*
     * Menu: return to the Launcher. Deferred to the timer, so the App
     * root is not deleted while its own key callback is still running.
     */
    s_back_pending = true;
}

static void settings_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) {
        return;
    }

    const uint32_t key = lv_event_get_key(event);

    switch (s_view) {
    case SETTINGS_VIEW_WIFI:
        settings_wifi_key(key);
        return;
    case SETTINGS_VIEW_WIFI_PROVISIONING:
        /* The provisioning page only answers to B. */
        if (key == LV_KEY_ESC) {
            settings_handle_escape();
        }
        return;
    case SETTINGS_VIEW_MENU:
        break;
    default:
        /* Status pages only accept B; arrows and A do nothing. */
        if (key == LV_KEY_ESC) {
            settings_handle_escape();
        }
        return;
    }

    switch (key) {
    case LV_KEY_UP:
        settings_menu_move(-1);
        break;
    case LV_KEY_DOWN:
        settings_menu_move(1);
        break;
    case LV_KEY_ENTER:
        settings_menu_activate();
        break;
    case LV_KEY_ESC:
        settings_handle_escape();
        break;
    default:
        /* Left and right have no meaning inside a single-column menu. */
        break;
    }
}

/*
 * Runs from lv_timer_handler(): clears the B latch on release and, when
 * the menu asked for it, closes the App from here rather than from the
 * key callback. lv_timer_delete() is safe from the timer's own callback,
 * which is what settings_close() does during that close.
 */
static void settings_b_release_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_b_latched && s_keypad != NULL &&
        lv_indev_get_state(s_keypad) == LV_INDEV_STATE_RELEASED) {
        s_b_latched = false;
    }

    /*
     * The provisioning page shows live progress. The refresh rides on
     * the timer that already exists, so no second timer is created and
     * nothing is left behind when the App closes (goal node 10,
     * checkpoint 4).
     */
    if (s_view == SETTINGS_VIEW_WIFI_PROVISIONING) {
        if (++s_wifi_refresh_ticks >= SETTINGS_WIFI_REFRESH_TICKS) {
            s_wifi_refresh_ticks = 0;
            settings_wifi_prov_refresh();
        }
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

static lv_indev_t *settings_find_keypad(void)
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev != NULL;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            return indev;
        }
    }

    return NULL;
}

static void settings_open(void)
{
    if (s_root != NULL) {
        ESP_LOGE(TAG, "settings is already open");
        return;
    }

    lv_obj_t *root_parent = xiaomiao_navigation_app_root();
    if (root_parent == NULL) {
        ESP_LOGE(TAG, "no App content root to build Settings in");
        return;
    }

    lv_group_t *group = lv_group_get_default();
    if (group == NULL) {
        ESP_LOGE(TAG, "LVGL default group is not ready");
        return;
    }

    lv_indev_t *keypad = settings_find_keypad();
    if (keypad == NULL) {
        ESP_LOGE(TAG, "keypad indev missing, cannot detect the B release");
        return;
    }

    lv_obj_t *root = lv_obj_create(root_parent);
    if (root == NULL) {
        ESP_LOGE(TAG, "settings root allocation failed");
        return;
    }
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(SETTINGS_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    s_root = root;

    lv_obj_t *content = lv_obj_create(root);
    if (content == NULL) {
        ESP_LOGE(TAG, "settings content allocation failed");
        lv_obj_delete(root);
        s_root = NULL;
        return;
    }
    lv_obj_remove_style_all(content);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    s_content = content;

    lv_timer_t *timer = lv_timer_create(settings_b_release_timer_cb,
                                        SETTINGS_B_RELEASE_POLL_MS, NULL);
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
    s_menu_index = 0;
    s_wifi_index = 0;
    s_wifi_mode = SETTINGS_WIFI_LIST;
    s_wifi_refresh_ticks = 0;
    settings_set_wifi_message("");
    s_b_latched = false;
    s_back_pending = false;

    lv_obj_add_event_cb(s_root, settings_key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(s_group, s_root);
    lv_group_focus_obj(s_root);

    /* Entering the App always starts on the menu with Wi-Fi focused. */
    settings_show_view(SETTINGS_VIEW_MENU);

    ESP_LOGI(TAG, "settings opened, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static void settings_close(void)
{
    /* Own resources first, then hand the object tree back to Navigation,
     * which deletes the content root after this callback returns
     * (goal decision 14). */
    if (s_b_release_timer != NULL) {
        lv_timer_delete(s_b_release_timer);
        s_b_release_timer = NULL;
    }

    if (s_root != NULL && s_group != NULL) {
        lv_group_remove_obj(s_root);
    }

    s_content = NULL;
    for (size_t i = 0; i < SETTINGS_MENU_ITEM_COUNT; ++i) {
        s_menu_items[i] = NULL;
    }
    for (size_t i = 0; i < SETTINGS_WIFI_ROW_COUNT; ++i) {
        s_wifi_rows[i] = NULL;
    }
    s_wifi_message = NULL;
    s_prov_line_ssid = NULL;
    s_prov_line_password = NULL;
    s_prov_line_url = NULL;
    s_prov_state = NULL;
    s_root = NULL;
    s_group = NULL;
    s_keypad = NULL;
    s_view = SETTINGS_VIEW_MENU;
    s_menu_index = 0;
    s_wifi_index = 0;
    s_wifi_mode = SETTINGS_WIFI_LIST;
    s_wifi_refresh_ticks = 0;
    s_b_latched = false;
    s_back_pending = false;

    ESP_LOGI(TAG, "settings closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static xiaomiao_app_t s_settings_app = {
    .id = SETTINGS_APP_ID,
    .name = NULL,
    .icon = SETTINGS_APP_ICON,
    .init = NULL,
    .open = settings_open,
    .close = settings_close,
};

const xiaomiao_app_t *xiaomiao_settings_app(void)
{
    /* The Launcher registers apps after the Font Service has run, so the
     * localized name is resolved on the first access. */
    s_settings_app.name = xiaomiao_text(XM_TEXT_APP_SETTINGS);
    return &s_settings_app;
}
