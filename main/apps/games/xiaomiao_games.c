/*
 * Games placeholder App (goal node 5).
 *
 * The page is created under the Navigation content root and removed by
 * Navigation when the App closes. The App never touches the global
 * screen, never joins the LVGL group and never registers a key handler,
 * so the Launcher root keeps the focus and provides the B path back
 * (goal decisions 5 to 9).
 */

#include "xiaomiao_games.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "framework/xiaomiao_fonts.h"
#include "framework/xiaomiao_i18n.h"
#include "framework/xiaomiao_navigation.h"

static const char TAG[] = "games";

#define GAMES_APP_ID    "games"
#define GAMES_APP_ICON  LV_SYMBOL_PLAY

/* Same palette as the Launcher so both screens look like one product. */
#define GAMES_COLOR_SCREEN_BG 0x0E1016
#define GAMES_COLOR_TITLE     0xC8D0E0
#define GAMES_COLOR_SUBTITLE  0x9AA6BC
#define GAMES_COLOR_HINT      0x5A6478

/*
 * 160 x 128 layout: title, subtitle and a key hint in the footer strip.
 * The gaps (24 / 52 / bottom -12) are larger than the localized line
 * heights (18 px body, 14 px small), so the three texts cannot overlap.
 */
#define GAMES_TITLE_Y     24
#define GAMES_SUBTITLE_Y  52
#define GAMES_HINT_Y      (-12)

/* Only the container is kept: it is the double-open guard and the only
 * reference the close path has to drop. The labels belong to Navigation
 * through their parent and are never touched again. */
static lv_obj_t *s_container;

static void games_create_label(lv_obj_t *parent, const char *text,
                               const lv_font_t *font, uint32_t color,
                               lv_align_t align, int32_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        ESP_LOGW(TAG, "placeholder label '%s' allocation failed", text);
        return;
    }

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    /* Full container width keeps the text centred without measuring it. */
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);
    lv_obj_align(label, align, 0, y);
}

static void games_open(void)
{
    if (s_container != NULL) {
        ESP_LOGE(TAG, "games placeholder is already open");
        return;
    }

    lv_obj_t *root = xiaomiao_navigation_app_root();
    if (root == NULL) {
        ESP_LOGE(TAG, "no App content root to build the placeholder in");
        return;
    }

    lv_obj_t *container = lv_obj_create(root);
    if (container == NULL) {
        ESP_LOGE(TAG, "placeholder container allocation failed");
        return;
    }
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container, lv_color_hex(GAMES_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    s_container = container;

    games_create_label(container, xiaomiao_text(XM_TEXT_APP_GAMES),
                       xiaomiao_font_title(),
                       GAMES_COLOR_TITLE, LV_ALIGN_TOP_MID, GAMES_TITLE_Y);
    games_create_label(container, xiaomiao_text(XM_TEXT_GAMES_COMING_SOON),
                       xiaomiao_font_body(),
                       GAMES_COLOR_SUBTITLE, LV_ALIGN_TOP_MID, GAMES_SUBTITLE_Y);
    games_create_label(container, xiaomiao_text(XM_TEXT_HINT_B_BACK),
                       xiaomiao_font_small(),
                       GAMES_COLOR_HINT, LV_ALIGN_BOTTOM_MID, GAMES_HINT_Y);

    /*
     * Observable for the lifecycle check (goal node 5, decision 12):
     * Navigation released the previous App content root before this
     * callback ran, so the active screen must hold exactly the Launcher
     * root and this content root. A growing count across entries means a
     * leaked root.
     */
    ESP_LOGI(TAG, "games opened, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

static void games_close(void)
{
    /* Navigation deletes the content root after this callback returns;
     * dropping the reference keeps the periodic path off freed objects. */
    s_container = NULL;

    /* Same observable as open, still counting the not-yet-deleted root. */
    ESP_LOGI(TAG, "games closed, screen children=%u",
             (unsigned)lv_obj_get_child_count(lv_screen_active()));
}

/*
 * Not const: `name` is the localized text resolved when the Launcher
 * asks for the App, i.e. after the Font Service has latched the
 * language. Registry and Navigation only ever see the pointer returned
 * by the accessor below.
 */
static xiaomiao_app_t s_games_app = {
    .id = GAMES_APP_ID,
    .name = NULL,
    .icon = GAMES_APP_ICON,
    .init = NULL,
    .open = games_open,
    .close = games_close,
};

const xiaomiao_app_t *xiaomiao_games_app(void)
{
    s_games_app.name = xiaomiao_text(XM_TEXT_APP_GAMES);
    return &s_games_app;
}
