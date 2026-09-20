/*
 * Tools App (goal node 7).
 *
 * First business App with an in-App menu and two navigation levels:
 * a menu page (Wi-Fi / System Info / About) plus three read-only detail
 * pages. Everything is built under the Navigation content root; the App
 * never creates, switches or deletes a global screen and never touches
 * hardware, a Service or a network API.
 *
 * Two boundary rules drive the design:
 * - System Info shows real values read once on entry from read-only
 *   ESP-IDF APIs. No value is hardcoded and no periodic refresh exists
 *   (goal decision 13).
 * - The Wi-Fi Service is not implemented before node 10, so that page
 *   states the capability is unavailable instead of inventing a
 *   connection state (goal decision 15).
 *
 * Input: the App takes the focus on its own root inside the LVGL default
 * group, so LVGL sends it the key events and the Launcher's key handler
 * stays unreachable while Tools is open.
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
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "framework/xiaomiao_navigation.h"

static const char TAG[] = "tools";

#define TOOLS_APP_ID    "tools"
#define TOOLS_APP_NAME  "Tools"
/* A list reads as "several entries" and does not clash with the Hardware
 * Test icon (LV_SYMBOL_SETTINGS). LV_SYMBOL_WRENCH does not exist in the
 * locked LVGL 9.5 (goal decision 3). */
#define TOOLS_APP_ICON  LV_SYMBOL_LIST

/* Same palette as the Launcher and PC Monitor. */
#define TOOLS_COLOR_SCREEN_BG      0x0E1016
#define TOOLS_COLOR_TITLE          0xC8D0E0
#define TOOLS_COLOR_TEXT           0x9AA6BC
#define TOOLS_COLOR_MUTED          0x5A6478
#define TOOLS_COLOR_ROW_BG         0x1B1F2A
#define TOOLS_COLOR_ROW_BORDER     0x39404F
#define TOOLS_COLOR_FOCUS_BG       0x2D6CDF
#define TOOLS_COLOR_FOCUS_BORDER   0xFFFFFF
#define TOOLS_COLOR_WARN           0xE0A030

/*
 * 160 x 128 layout. Title on top, footer hint at the bottom, content in
 * between. The montserrat_12 line height is 15 px so the title box is 16;
 * the montserrat_10 line height is 13 px so data rows are 15 px apart
 * (goal decision 11).
 */
#define TOOLS_SCREEN_W   160
#define TOOLS_TITLE_Y    2
#define TOOLS_TITLE_H    16
#define TOOLS_FOOTER_Y   110
#define TOOLS_FOOTER_H   14

#define TOOLS_MENU_X     6
#define TOOLS_MENU_W     148
#define TOOLS_MENU_Y0    22
#define TOOLS_MENU_STEP  26
#define TOOLS_MENU_H     24

#define TOOLS_INFO_Y0    20
#define TOOLS_INFO_STEP  15
#define TOOLS_INFO_H     14
#define TOOLS_INFO_NAME_X 4
#define TOOLS_INFO_NAME_W 44
#define TOOLS_INFO_VAL_X  50
#define TOOLS_INFO_VAL_W  106

#define TOOLS_ABOUT_X    6
#define TOOLS_ABOUT_Y    22
#define TOOLS_ABOUT_W    148
#define TOOLS_ABOUT_H    70

/* Poll period for the B release check; the latch itself is event driven. */
#define TOOLS_B_RELEASE_POLL_MS 20

typedef enum {
    TOOLS_VIEW_MENU = 0,
    TOOLS_VIEW_WIFI,
    TOOLS_VIEW_SYSTEM_INFO,
    TOOLS_VIEW_ABOUT,
} tools_view_t;

/* Menu order is the focus order: up/down move the index, A opens it. */
static const char *const s_menu_labels[] = {
    "Wi-Fi",
    "System Info",
    "About",
};
#define TOOLS_MENU_ITEM_COUNT (sizeof(s_menu_labels) / sizeof(s_menu_labels[0]))

#define TOOLS_MENU_ITEM_WIFI        0
#define TOOLS_MENU_ITEM_SYSTEM_INFO 1
#define TOOLS_MENU_ITEM_ABOUT       2

/* The input root owns the focus; the content container is rebuilt per
 * view and never holds the focus itself (goal decisions 6 and 12). */
static lv_obj_t *s_root;
static lv_obj_t *s_content;
static lv_obj_t *s_menu_items[TOOLS_MENU_ITEM_COUNT];
static lv_group_t *s_group;
static lv_indev_t *s_keypad;
static lv_timer_t *s_b_release_timer;
static tools_view_t s_view = TOOLS_VIEW_MENU;
static size_t s_menu_index;
static bool s_b_latched;
static bool s_back_pending;

static void tools_show_view(tools_view_t view);

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

static lv_obj_t *tools_create_page_title(lv_obj_t *parent, const char *text)
{
    return tools_place_label(parent, text, &lv_font_montserrat_12,
                             TOOLS_COLOR_TITLE, 0, TOOLS_TITLE_Y, TOOLS_SCREEN_W,
                             TOOLS_TITLE_H, LV_TEXT_ALIGN_CENTER);
}

static void tools_create_footer(lv_obj_t *parent, const char *text)
{
    tools_place_label(parent, text, &lv_font_montserrat_10, TOOLS_COLOR_MUTED, 0,
                      TOOLS_FOOTER_Y, TOOLS_SCREEN_W, TOOLS_FOOTER_H,
                      LV_TEXT_ALIGN_CENTER);
}

static void tools_menu_highlight(void)
{
    if (s_view != TOOLS_VIEW_MENU) {
        return;
    }

    for (size_t i = 0; i < TOOLS_MENU_ITEM_COUNT; ++i) {
        lv_obj_t *row = s_menu_items[i];
        if (row == NULL) {
            continue;
        }

        const bool focused = (i == s_menu_index);
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(focused ? TOOLS_COLOR_FOCUS_BG
                                                       : TOOLS_COLOR_ROW_BG),
                                  0);
        lv_obj_set_style_border_color(row,
                                      lv_color_hex(focused ? TOOLS_COLOR_FOCUS_BORDER
                                                           : TOOLS_COLOR_ROW_BORDER),
                                      0);
        lv_obj_set_style_border_width(row, focused ? 2 : 1, 0);
    }
}

static void tools_build_menu(lv_obj_t *content)
{
    tools_create_page_title(content, TOOLS_APP_NAME);

    for (size_t i = 0; i < TOOLS_MENU_ITEM_COUNT; ++i) {
        const int32_t y = TOOLS_MENU_Y0 + (int32_t)i * TOOLS_MENU_STEP;

        lv_obj_t *row = lv_obj_create(content);
        if (row == NULL) {
            ESP_LOGW(TAG, "menu row allocation failed");
            continue;
        }
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, TOOLS_MENU_X, y);
        lv_obj_set_size(row, TOOLS_MENU_W, TOOLS_MENU_H);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        /* Decoration only: the root below stays the single focus object. */
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *label = tools_create_label(row, s_menu_labels[i],
                                             &lv_font_montserrat_12,
                                             TOOLS_COLOR_TITLE,
                                             LV_LABEL_LONG_MODE_DOTS);
        if (label != NULL) {
            lv_obj_set_size(label, TOOLS_MENU_W - 16, 16);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
        }

        s_menu_items[i] = row;
    }

    tools_menu_highlight();
    tools_create_footer(content, "A Open  B Back");
}

static void tools_build_wifi(lv_obj_t *content)
{
    tools_create_page_title(content, "Wi-Fi");

    tools_place_label(content, "Wi-Fi Service", &lv_font_montserrat_12,
                      TOOLS_COLOR_TEXT, 0, 34, TOOLS_SCREEN_W, 16,
                      LV_TEXT_ALIGN_CENTER);
    /*
     * Real capability state, not a network state: the Service does not
     * exist yet, so neither "Connected" nor "Disconnected" would be true
     * (goal decision 15).
     */
    tools_place_label(content, "Unavailable", &lv_font_montserrat_12,
                      TOOLS_COLOR_WARN, 0, 56, TOOLS_SCREEN_W, 16,
                      LV_TEXT_ALIGN_CENTER);
    tools_place_label(content, "Implemented in node 10", &lv_font_montserrat_10,
                      TOOLS_COLOR_MUTED, 0, 80, TOOLS_SCREEN_W, 14,
                      LV_TEXT_ALIGN_CENTER);

    tools_create_footer(content, "B Back");
}

static const char *tools_chip_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    default:
        return "Unknown";
    }
}

/* Flash capacity, or "Unknown" with the error code logged. Never a guess. */
static void tools_flash_capacity(char *out, size_t out_len)
{
    uint32_t bytes = 0;
    const esp_err_t err = esp_flash_get_size(NULL, &bytes);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flash size read failed: %s (0x%x)", esp_err_to_name(err),
                 (unsigned)err);
        snprintf(out, out_len, "Unknown");
        return;
    }

    snprintf(out, out_len, "%u MiB", (unsigned)(bytes / (1024U * 1024U)));
}

/* PSRAM capacity; a missing PSRAM is "None", not a startup error. */
static void tools_psram_capacity(char *out, size_t out_len)
{
    const size_t bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    if (bytes == 0) {
        snprintf(out, out_len, "None");
        return;
    }

    snprintf(out, out_len, "%u MiB", (unsigned)(bytes / (1024U * 1024U)));
}

/* Build version from the application description, not a hardcoded tag. */
static void tools_firmware_version(char *out, size_t out_len)
{
    const esp_app_desc_t *desc = esp_app_get_description();

    if (desc == NULL) {
        ESP_LOGW(TAG, "application description unavailable");
        snprintf(out, out_len, "Unknown");
        return;
    }

    snprintf(out, out_len, "%.*s", (int)sizeof(desc->version), desc->version);
}

static void tools_build_system_info(lv_obj_t *content)
{
    char chip[24];
    char flash[24];
    char psram[24];
    char firmware[40];
    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);
    tools_flash_capacity(flash, sizeof(flash));
    tools_psram_capacity(psram, sizeof(psram));
    tools_firmware_version(firmware, sizeof(firmware));
    snprintf(chip, sizeof(chip), "%s x%u", tools_chip_name(chip_info.model),
             (unsigned)chip_info.cores);

    char cpu[24];
    snprintf(cpu, sizeof(cpu), "%d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);

    const char *const names[] = { "Chip", "CPU", "Flash", "PSRAM", "IDF", "FW" };
    const char *values[] = { chip, cpu, flash, psram, esp_get_idf_version(),
                             firmware };

    tools_create_page_title(content, "System Info");

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const int32_t y = TOOLS_INFO_Y0 + (int32_t)i * TOOLS_INFO_STEP;

        tools_place_label(content, names[i], &lv_font_montserrat_10,
                          TOOLS_COLOR_TEXT, TOOLS_INFO_NAME_X, y,
                          TOOLS_INFO_NAME_W, TOOLS_INFO_H, LV_TEXT_ALIGN_LEFT);
        tools_place_label(content, values[i], &lv_font_montserrat_10,
                          TOOLS_COLOR_TITLE, TOOLS_INFO_VAL_X, y,
                          TOOLS_INFO_VAL_W, TOOLS_INFO_H, LV_TEXT_ALIGN_RIGHT);
    }

    tools_create_footer(content, "B Back");
}

static void tools_build_about(lv_obj_t *content)
{
    char firmware[40];
    char text[192];

    tools_firmware_version(firmware, sizeof(firmware));
    snprintf(text, sizeof(text),
             "Project: Xiaomiao\n"
             "Firmware: %s\n"
             "Author: ZYoungInc\n"
             "Repo: nbh847/xueersi-idf",
             firmware);

    tools_create_page_title(content, "About");

    /* Explicit width plus wrapping; nothing is loaded from disk or network. */
    lv_obj_t *body = tools_create_label(content, text, &lv_font_montserrat_10,
                                        TOOLS_COLOR_TEXT,
                                        LV_LABEL_LONG_MODE_WRAP);
    if (body != NULL) {
        lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(body, TOOLS_ABOUT_X, TOOLS_ABOUT_Y);
        lv_obj_set_size(body, TOOLS_ABOUT_W, TOOLS_ABOUT_H);
    }

    tools_create_footer(content, "B Back");
}

static void tools_show_view(tools_view_t view)
{
    if (s_content == NULL) {
        ESP_LOGE(TAG, "no content container for view %d", (int)view);
        return;
    }

    /* Only the Tools content is rebuilt; the input root and the
     * Navigation content root stay untouched (goal decision 6). */
    lv_obj_clean(s_content);
    for (size_t i = 0; i < TOOLS_MENU_ITEM_COUNT; ++i) {
        s_menu_items[i] = NULL;
    }

    s_view = view;

    switch (view) {
    case TOOLS_VIEW_MENU:
        tools_build_menu(s_content);
        break;
    case TOOLS_VIEW_WIFI:
        tools_build_wifi(s_content);
        break;
    case TOOLS_VIEW_SYSTEM_INFO:
        tools_build_system_info(s_content);
        break;
    case TOOLS_VIEW_ABOUT:
        tools_build_about(s_content);
        break;
    }

    ESP_LOGD(TAG, "view=%d", (int)view);
}

static void tools_menu_move(int step)
{
    if (step < 0) {
        if (s_menu_index == 0) {
            return;
        }
        s_menu_index--;
    }
    else {
        if (s_menu_index + 1 >= TOOLS_MENU_ITEM_COUNT) {
            return;
        }
        s_menu_index++;
    }

    tools_menu_highlight();
}

static void tools_menu_activate(void)
{
    switch (s_menu_index) {
    case TOOLS_MENU_ITEM_WIFI:
        tools_show_view(TOOLS_VIEW_WIFI);
        break;
    case TOOLS_MENU_ITEM_SYSTEM_INFO:
        tools_show_view(TOOLS_VIEW_SYSTEM_INFO);
        break;
    case TOOLS_MENU_ITEM_ABOUT:
        tools_show_view(TOOLS_VIEW_ABOUT);
        break;
    default:
        break;
    }
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

    if (s_view != TOOLS_VIEW_MENU) {
        tools_show_view(TOOLS_VIEW_MENU);
        return;
    }

    /*
     * Menu: return to the Launcher. Deferred to the timer, so the App
     * root is not deleted while its own key callback is still running.
     */
    s_back_pending = true;
}

static void tools_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) {
        return;
    }

    const uint32_t key = lv_event_get_key(event);

    if (s_view != TOOLS_VIEW_MENU) {
        /* Detail pages only accept B; arrows and A do nothing. */
        if (key == LV_KEY_ESC) {
            tools_handle_escape();
        }
        return;
    }

    switch (key) {
    case LV_KEY_UP:
        tools_menu_move(-1);
        break;
    case LV_KEY_DOWN:
        tools_menu_move(1);
        break;
    case LV_KEY_ENTER:
        tools_menu_activate();
        break;
    case LV_KEY_ESC:
        tools_handle_escape();
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
    s_menu_index = 0;
    s_b_latched = false;
    s_back_pending = false;

    lv_obj_add_event_cb(s_root, tools_key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(s_group, s_root);
    lv_group_focus_obj(s_root);

    /* Entering the App always starts on the menu with Wi-Fi focused. */
    tools_show_view(TOOLS_VIEW_MENU);

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
    for (size_t i = 0; i < TOOLS_MENU_ITEM_COUNT; ++i) {
        s_menu_items[i] = NULL;
    }
    s_root = NULL;
    s_group = NULL;
    s_keypad = NULL;
    s_view = TOOLS_VIEW_MENU;
    s_menu_index = 0;
    s_b_latched = false;
    s_back_pending = false;

    ESP_LOGI(TAG, "tools closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static const xiaomiao_app_t s_tools_app = {
    .id = TOOLS_APP_ID,
    .name = TOOLS_APP_NAME,
    .icon = TOOLS_APP_ICON,
    .init = NULL,
    .open = tools_open,
    .close = tools_close,
};

const xiaomiao_app_t *xiaomiao_tools_app(void)
{
    return &s_tools_app;
}
