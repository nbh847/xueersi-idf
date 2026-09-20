/*
 * Settings App (goal nodes 8 and 9).
 *
 * UI skeleton plus the first real Service read: an in-App menu
 * (Wi-Fi / Display / Sound / System) with four read-only detail pages.
 * Everything is built under the Navigation content root; the App never
 * creates, switches or deletes a global screen and never touches
 * hardware, NVS or a network API.
 *
 * Boundary rules:
 * - Only two pages talk about a missing capability (Wi-Fi, Sound) and
 *   they name the node that will deliver it. Neither offers a switch,
 *   because the preferences the Settings Service stores have no effect
 *   until nodes 10 and 12 consume them (goal node 9, decision 3).
 * - Display states a hardware fact instead of a missing setting: the
 *   backlight is tied to VCC, so there is no brightness to store (goal
 *   node 9, decision 4).
 * - System is the only page that reads the Settings Service, and it
 *   reads it once per entry without creating a timer. It reports the
 *   real persistence state, so a degraded boot can never be shown as
 *   "saved" (goal node 9, decisions 5 and 6).
 * - Nothing reads hardware to fabricate a state: there is no
 *   "Connected" and no volume anywhere.
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
 * object is never deleted from inside its own key callback.
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

#include "framework/xiaomiao_navigation.h"
#include "services/xiaomiao_settings_service.h"

static const char TAG[] = "settings";

#define SETTINGS_APP_ID    "settings"
#define SETTINGS_APP_NAME  "Settings"
/* An edit/pencil glyph reads as "configuration" and does not clash with
 * the Hardware Test icon (LV_SYMBOL_SETTINGS); the Launcher renders
 * icons with montserrat_12, which carries LV_SYMBOL_EDIT (goal
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
 * rows in between. The montserrat_12 line height is 15 px, so 20 px rows
 * with a 22 px step hold the text without clipping; the last row ends at
 * y=106, leaving the footer at y=110 clear (goal decision 8).
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

/* Longest System detail line, e.g. "NVS error 0x110C". */
#define SETTINGS_SYSTEM_DETAIL_MAX 24

/* Poll period for the B release check; the latch itself is event driven. */
#define SETTINGS_B_RELEASE_POLL_MS 20

typedef enum {
    SETTINGS_VIEW_MENU = 0,
    SETTINGS_VIEW_WIFI,
    SETTINGS_VIEW_DISPLAY,
    SETTINGS_VIEW_SOUND,
    SETTINGS_VIEW_SYSTEM,
} settings_view_t;

/* Menu order is the focus order: up/down move the index, A opens it. */
static const char *const s_menu_labels[] = {
    "Wi-Fi",
    "Display",
    "Sound",
    "System",
};
#define SETTINGS_MENU_ITEM_COUNT (sizeof(s_menu_labels) / sizeof(s_menu_labels[0]))

#define SETTINGS_MENU_ITEM_WIFI    0
#define SETTINGS_MENU_ITEM_DISPLAY 1
#define SETTINGS_MENU_ITEM_SOUND   2
#define SETTINGS_MENU_ITEM_SYSTEM  3

/* The input root owns the focus; the content container is rebuilt per
 * view and never holds the focus itself (goal decisions 5 and 6). */
static lv_obj_t *s_root;
static lv_obj_t *s_content;
static lv_obj_t *s_menu_items[SETTINGS_MENU_ITEM_COUNT];
static lv_group_t *s_group;
static lv_indev_t *s_keypad;
static lv_timer_t *s_b_release_timer;
static settings_view_t s_view = SETTINGS_VIEW_MENU;
static size_t s_menu_index;
static bool s_b_latched;
static bool s_back_pending;

static void settings_show_view(settings_view_t view);

static lv_obj_t *settings_create_label(lv_obj_t *parent, const char *text,
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
    return settings_place_label(parent, text, &lv_font_montserrat_12,
                                SETTINGS_COLOR_TITLE, 0, SETTINGS_TITLE_Y,
                                SETTINGS_SCREEN_W, SETTINGS_TITLE_H,
                                LV_TEXT_ALIGN_CENTER);
}

static void settings_create_footer(lv_obj_t *parent, const char *text)
{
    settings_place_label(parent, text, &lv_font_montserrat_10,
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
    settings_create_page_title(content, SETTINGS_APP_NAME);

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

        lv_obj_t *label = settings_create_label(row, s_menu_labels[i],
                                                &lv_font_montserrat_12,
                                                SETTINGS_COLOR_TITLE,
                                                LV_LABEL_LONG_MODE_DOTS);
        if (label != NULL) {
            lv_obj_set_size(label, SETTINGS_MENU_W - 16, 16);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
        }

        s_menu_items[i] = row;
    }

    settings_menu_highlight();
    settings_create_footer(content, "A Open  B Back");
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

    settings_place_label(content, headline, &lv_font_montserrat_12,
                         SETTINGS_COLOR_TEXT, 0, SETTINGS_STATUS_HEAD_Y,
                         SETTINGS_SCREEN_W, SETTINGS_STATUS_LINE_H,
                         LV_TEXT_ALIGN_CENTER);
    settings_place_label(content, state, &lv_font_montserrat_12, state_color,
                         0, SETTINGS_STATUS_STATE_Y, SETTINGS_SCREEN_W,
                         SETTINGS_STATUS_LINE_H, LV_TEXT_ALIGN_CENTER);
    settings_place_label(content, detail, &lv_font_montserrat_10,
                         SETTINGS_COLOR_MUTED, 0, SETTINGS_STATUS_DETAIL_Y,
                         SETTINGS_SCREEN_W, SETTINGS_STATUS_DETAIL_H,
                         LV_TEXT_ALIGN_CENTER);

    settings_create_footer(content, "B Back");
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
        state = "Unavailable";
        state_color = SETTINGS_COLOR_WARN;
        snprintf(detail, sizeof(detail), "Settings unavailable");
    }
    else {
        switch (xiaomiao_settings_source()) {
        case XIAOMIAO_SETTINGS_SOURCE_NVS:
            state = "Loaded from NVS";
            state_color = SETTINGS_COLOR_TEXT;
            break;
        case XIAOMIAO_SETTINGS_SOURCE_RECOVERED:
            state = "Defaults restored";
            state_color = SETTINGS_COLOR_WARN;
            break;
        case XIAOMIAO_SETTINGS_SOURCE_DEGRADED:
            state = "Not persisted";
            state_color = SETTINGS_COLOR_WARN;
            break;
        case XIAOMIAO_SETTINGS_SOURCE_DEFAULTS:
        default:
            state = "Defaults applied";
            state_color = SETTINGS_COLOR_TEXT;
            break;
        }

        const esp_err_t last_err = xiaomiao_settings_last_error();
        if (last_err != ESP_OK) {
            snprintf(detail, sizeof(detail), "NVS error 0x%X", (unsigned)last_err);
        }
        else {
            snprintf(detail, sizeof(detail), "2 stored fields");
        }
    }

    settings_build_detail(content, "System", "Settings Service", state,
                          state_color, detail);
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

    s_view = view;

    switch (view) {
    case SETTINGS_VIEW_MENU:
        settings_build_menu(s_content);
        break;
    case SETTINGS_VIEW_WIFI:
        settings_build_detail(s_content, "Wi-Fi", "Wi-Fi Service",
                              "Unavailable", SETTINGS_COLOR_WARN,
                              "Implemented in node 10");
        break;
    case SETTINGS_VIEW_DISPLAY:
        /* Hardware fact, not a missing setting: the backlight is wired
         * to VCC, so there is no brightness to store or restore. */
        settings_build_detail(s_content, "Display", "Brightness fixed",
                              "Backlight tied to VCC", SETTINGS_COLOR_TEXT,
                              "No display settings");
        break;
    case SETTINGS_VIEW_SOUND:
        settings_build_detail(s_content, "Sound", "Audio Service",
                              "Unavailable", SETTINGS_COLOR_WARN,
                              "Implemented in node 12");
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

    if (s_view != SETTINGS_VIEW_MENU) {
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
    s_root = NULL;
    s_group = NULL;
    s_keypad = NULL;
    s_view = SETTINGS_VIEW_MENU;
    s_menu_index = 0;
    s_b_latched = false;
    s_back_pending = false;

    ESP_LOGI(TAG, "settings closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static const xiaomiao_app_t s_settings_app = {
    .id = SETTINGS_APP_ID,
    .name = SETTINGS_APP_NAME,
    .icon = SETTINGS_APP_ICON,
    .init = NULL,
    .open = settings_open,
    .close = settings_close,
};

const xiaomiao_app_t *xiaomiao_settings_app(void)
{
    return &s_settings_app;
}
