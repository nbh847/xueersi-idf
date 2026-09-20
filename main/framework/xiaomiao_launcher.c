#include "xiaomiao_launcher.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_log.h"
#include "xiaomiao_app.h"
#include "xiaomiao_navigation.h"

static const char TAG[] = "launcher";

/*
 * 160 x 128 layout (goal checkpoint 2): title strip on top, a 2 x 2
 * entry grid in the middle and a footer strip with a key hint and the
 * page indicator. Coordinates are fixed so long App names cannot push
 * neighbouring entries around.
 */
#define LAUNCHER_TITLE_X      5
#define LAUNCHER_TITLE_Y      2
#define LAUNCHER_TITLE_W      150
#define LAUNCHER_TITLE_H      14
#define LAUNCHER_CARD_W       74
#define LAUNCHER_CARD_H       42
#define LAUNCHER_CARD_X0      4
#define LAUNCHER_CARD_X1      82
#define LAUNCHER_CARD_Y0      19
#define LAUNCHER_CARD_Y1      65
#define LAUNCHER_FOOTER_Y     110
#define LAUNCHER_FOOTER_H     14

#define LAUNCHER_COLOR_SCREEN_BG     0x0E1016
#define LAUNCHER_COLOR_CARD_BG       0x1B1F2A
#define LAUNCHER_COLOR_CARD_BORDER   0x39404F
#define LAUNCHER_COLOR_TEXT          0xC8D0E0
#define LAUNCHER_COLOR_ICON          0x8FA8D8
#define LAUNCHER_COLOR_PLACEHOLDER   0x5A6478
#define LAUNCHER_COLOR_FOCUS_BG      0x2D6CDF
#define LAUNCHER_COLOR_FOCUS_BORDER  0xFFFFFF
#define LAUNCHER_COLOR_CHROME        0x9AA6BC

/* One grid entry: the card plus its icon, placeholder and name label. */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *icon;
    lv_obj_t *placeholder;
    lv_obj_t *name;
} launcher_slot_t;

/* The Launcher only navigates horizontally; there is no vertical move. */
typedef enum {
    LAUNCHER_MOVE_LEFT = 0,
    LAUNCHER_MOVE_RIGHT,
} launcher_move_t;

static lv_obj_t *s_root;
static lv_obj_t *s_hint;
static lv_obj_t *s_page;
static lv_obj_t *s_empty;
static launcher_slot_t s_slots[XIAOMIAO_LAUNCHER_PER_PAGE];
static size_t s_focus;

static bool launcher_app_open(void)
{
    return xiaomiao_navigation_current() != NULL;
}

static void launcher_slot_set_focused(launcher_slot_t *slot, bool focused)
{
    lv_obj_set_style_bg_color(slot->card,
                              lv_color_hex(focused ? LAUNCHER_COLOR_FOCUS_BG
                                                   : LAUNCHER_COLOR_CARD_BG),
                              0);
    lv_obj_set_style_border_color(slot->card,
                                  lv_color_hex(focused ? LAUNCHER_COLOR_FOCUS_BORDER
                                                       : LAUNCHER_COLOR_CARD_BORDER),
                                  0);
    lv_obj_set_style_border_width(slot->card, focused ? 2 : 1, 0);
    lv_obj_set_style_text_color(slot->name,
                                lv_color_hex(focused ? LAUNCHER_COLOR_FOCUS_BORDER
                                                     : LAUNCHER_COLOR_TEXT),
                                0);
}

static void launcher_slot_fill(launcher_slot_t *slot, const xiaomiao_app_t *app)
{
    lv_label_set_text(slot->name, app->name != NULL ? app->name : "");

    if (app->icon != NULL && app->icon[0] != '\0') {
        lv_label_set_text(slot->icon, app->icon);
        lv_obj_clear_flag(slot->icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(slot->placeholder, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* No icon: show the uniform, resource-free placeholder bar. */
        lv_obj_add_flag(slot->icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(slot->placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * Render the page that contains s_focus. Fills or hides all four fixed
 * slots and refreshes the page indicator. Only called on create and when
 * focus crosses a page boundary, so same-page moves never rebuild the
 * Launcher (goal decision 6).
 */
static void launcher_render_page(void)
{
    const size_t count = xiaomiao_app_registry_count();

    if (count == 0) {
        for (size_t i = 0; i < XIAOMIAO_LAUNCHER_PER_PAGE; ++i) {
            lv_obj_add_flag(s_slots[i].card, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_page, "0/0");
        return;
    }

    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    const size_t page = s_focus / XIAOMIAO_LAUNCHER_PER_PAGE;
    const size_t base = page * XIAOMIAO_LAUNCHER_PER_PAGE;

    for (size_t i = 0; i < XIAOMIAO_LAUNCHER_PER_PAGE; ++i) {
        const size_t index = base + i;
        if (index < count) {
            launcher_slot_fill(&s_slots[i], xiaomiao_app_registry_get_at(index));
            lv_obj_clear_flag(s_slots[i].card, LV_OBJ_FLAG_HIDDEN);
            launcher_slot_set_focused(&s_slots[i], index == s_focus);
        } else {
            lv_obj_add_flag(s_slots[i].card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const size_t pages = (count + XIAOMIAO_LAUNCHER_PER_PAGE - 1) /
                         XIAOMIAO_LAUNCHER_PER_PAGE;
    lv_label_set_text_fmt(s_page, "%u/%u", (unsigned)(page + 1), (unsigned)pages);
}

/*
 * Target index for one horizontal move. The Launcher reads the Registry
 * as a single left-to-right sequence: the four slots of a page are
 * visited in row-major order (0, 1, 2, 3) and stepping past the fourth
 * one turns the page. Both ends clamp instead of wrapping, so the focus
 * never leaves the valid range (launcher L/R paging goal, decisions 1
 * and 2).
 */
static size_t launcher_next_index(launcher_move_t direction)
{
    const size_t count = xiaomiao_app_registry_count();
    const size_t index = s_focus;

    if (count == 0) {
        return index;
    }

    if (direction == LAUNCHER_MOVE_LEFT) {
        return (index > 0) ? index - 1 : index;
    }

    return (index + 1 < count) ? index + 1 : index;
}

static void launcher_move(launcher_move_t direction)
{
    if (xiaomiao_app_registry_count() == 0) {
        return;
    }

    const size_t old_index = s_focus;
    const size_t next = launcher_next_index(direction);
    if (next == old_index) {
        return;
    }

    const size_t old_page = old_index / XIAOMIAO_LAUNCHER_PER_PAGE;
    const size_t new_page = next / XIAOMIAO_LAUNCHER_PER_PAGE;

    s_focus = next;

    if (old_page != new_page) {
        launcher_render_page();
        return;
    }

    /* Same page: repaint only the two affected entries. */
    launcher_slot_set_focused(&s_slots[old_index % XIAOMIAO_LAUNCHER_PER_PAGE], false);
    launcher_slot_set_focused(&s_slots[next % XIAOMIAO_LAUNCHER_PER_PAGE], true);
}

/*
 * Open the focused App through Navigation. On failure the focus, page
 * and every Launcher object stay exactly as they were; the error is
 * logged with the App id and the numeric error code (goal decision 7).
 */
static void launcher_activate(void)
{
    const size_t count = xiaomiao_app_registry_count();
    if (count == 0) {
        return;
    }
    if (s_focus >= count) {
        ESP_LOGE(TAG, "focus %u out of range (count %u)", (unsigned)s_focus,
                 (unsigned)count);
        return;
    }

    const xiaomiao_app_t *app = xiaomiao_app_registry_get_at(s_focus);
    if (app == NULL || app->id == NULL || app->id[0] == '\0') {
        ESP_LOGE(TAG, "entry %u has no valid id", (unsigned)s_focus);
        return;
    }

    const esp_err_t err = xiaomiao_navigation_open(app->id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open '%s' failed: %s (0x%x)", app->id, esp_err_to_name(err),
                 (unsigned)err);
        return;
    }

    ESP_LOGI(TAG, "opened '%s'", app->id);
}

/*
 * Return to the Launcher. Navigation makes this idempotent on the root
 * page, so focus, page and object count are untouched either way (goal
 * decision 9).
 */
static void launcher_go_back(void)
{
    const esp_err_t err = xiaomiao_navigation_back();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "back failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    }
}

static void launcher_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY) {
        return;
    }

    const uint32_t key = lv_event_get_key(event);

    if (launcher_app_open()) {
        /*
         * An App is open and owns the screen. While it runs, the
         * Launcher only provides the B path back through Navigation;
         * arrow keys and A must not move or re-open anything.
         */
        if (key == LV_KEY_ESC) {
            launcher_go_back();
        }
        return;
    }

    switch (key) {
    case LV_KEY_LEFT:
        launcher_move(LAUNCHER_MOVE_LEFT);
        break;
    case LV_KEY_RIGHT:
        launcher_move(LAUNCHER_MOVE_RIGHT);
        break;
    case LV_KEY_ENTER:
        launcher_activate();
        break;
    case LV_KEY_ESC:
        launcher_go_back();
        break;
    default:
        /*
         * Up and down are deliberately not navigation keys: the
         * Launcher is a left/right pager, so a second way to change the
         * page would give the same transition two entry points
         * (launcher L/R paging goal, decision 3).
         */
        break;
    }
}

static void launcher_build_slot(lv_obj_t *root, size_t slot, int x, int y)
{
    launcher_slot_t *entry = &s_slots[slot];

    entry->card = lv_obj_create(root);
    lv_obj_remove_style_all(entry->card);
    lv_obj_set_size(entry->card, LAUNCHER_CARD_W, LAUNCHER_CARD_H);
    lv_obj_set_pos(entry->card, x, y);
    lv_obj_set_style_bg_opa(entry->card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(entry->card, lv_color_hex(LAUNCHER_COLOR_CARD_BG), 0);
    lv_obj_set_style_border_width(entry->card, 1, 0);
    lv_obj_set_style_border_color(entry->card,
                                  lv_color_hex(LAUNCHER_COLOR_CARD_BORDER), 0);
    lv_obj_set_style_radius(entry->card, 3, 0);
    lv_obj_set_style_pad_all(entry->card, 0, 0);
    lv_obj_clear_flag(entry->card, LV_OBJ_FLAG_SCROLLABLE);

    entry->icon = lv_label_create(entry->card);
    lv_obj_set_style_text_font(entry->icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(entry->icon, lv_color_hex(LAUNCHER_COLOR_ICON), 0);
    lv_obj_align(entry->icon, LV_ALIGN_TOP_MID, 0, 4);

    entry->placeholder = lv_obj_create(entry->card);
    lv_obj_remove_style_all(entry->placeholder);
    lv_obj_set_size(entry->placeholder, 20, 4);
    lv_obj_set_style_radius(entry->placeholder, 2, 0);
    lv_obj_set_style_bg_opa(entry->placeholder, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(entry->placeholder,
                              lv_color_hex(LAUNCHER_COLOR_PLACEHOLDER), 0);
    lv_obj_align(entry->placeholder, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_clear_flag(entry->placeholder, LV_OBJ_FLAG_SCROLLABLE);

    entry->name = lv_label_create(entry->card);
    lv_obj_set_style_text_font(entry->name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(entry->name, lv_color_hex(LAUNCHER_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(entry->name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(entry->name, LAUNCHER_CARD_W - 8);
    /* Deterministic truncation: long names get dots, never overlap. */
    lv_label_set_long_mode(entry->name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(entry->name, LV_ALIGN_BOTTOM_MID, 0, -3);
}

static void launcher_build_ui(lv_obj_t *root)
{
    static const int column_x[XIAOMIAO_LAUNCHER_COLUMNS] = { LAUNCHER_CARD_X0,
                                                             LAUNCHER_CARD_X1 };
    static const int row_y[XIAOMIAO_LAUNCHER_ROWS] = { LAUNCHER_CARD_Y0,
                                                       LAUNCHER_CARD_Y1 };

    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(LAUNCHER_COLOR_CHROME), 0);
    lv_obj_set_pos(title, LAUNCHER_TITLE_X, LAUNCHER_TITLE_Y);
    lv_obj_set_size(title, LAUNCHER_TITLE_W, LAUNCHER_TITLE_H);
    lv_label_set_text(title, "Xiaomiao");

    for (size_t i = 0; i < XIAOMIAO_LAUNCHER_PER_PAGE; ++i) {
        launcher_build_slot(root, i, column_x[i % XIAOMIAO_LAUNCHER_COLUMNS],
                            row_y[i / XIAOMIAO_LAUNCHER_COLUMNS]);
    }

    s_empty = lv_label_create(root);
    lv_obj_set_style_text_font(s_empty, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_empty, lv_color_hex(LAUNCHER_COLOR_CHROME), 0);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_empty, LAUNCHER_TITLE_W);
    lv_label_set_text(s_empty, "no apps registered");
    lv_obj_align(s_empty, LV_ALIGN_CENTER, 0, 0);

    s_hint = lv_label_create(root);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(LAUNCHER_COLOR_CHROME), 0);
    lv_obj_set_pos(s_hint, LAUNCHER_CARD_X0, LAUNCHER_FOOTER_Y);
    lv_obj_set_size(s_hint, LAUNCHER_CARD_W, LAUNCHER_FOOTER_H);
    lv_label_set_text(s_hint, "A open");

    s_page = lv_label_create(root);
    lv_obj_set_style_text_font(s_page, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_page, lv_color_hex(LAUNCHER_COLOR_CHROME), 0);
    lv_obj_set_style_text_align(s_page, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_page, LAUNCHER_CARD_X1, LAUNCHER_FOOTER_Y);
    lv_obj_set_size(s_page, LAUNCHER_CARD_W, LAUNCHER_FOOTER_H);
    lv_label_set_text(s_page, "0/0");
}

esp_err_t xiaomiao_launcher_create(lv_group_t *group)
{
    if (group == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_root != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lv_obj_t *root = lv_obj_create(lv_screen_active());
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(LAUNCHER_COLOR_SCREEN_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    /* Clickable so the focused root reliably receives key events. */
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);

    s_root = root;
    s_focus = 0;

    launcher_build_ui(root);
    launcher_render_page();

    lv_group_add_obj(group, root);
    lv_group_focus_obj(root);
    lv_obj_add_event_cb(root, launcher_key_cb, LV_EVENT_KEY, NULL);

    ESP_LOGI(TAG, "launcher created (%u apps)", (unsigned)xiaomiao_app_registry_count());
    return ESP_OK;
}

esp_err_t xiaomiao_launcher_destroy(void)
{
    if (s_root == NULL) {
        return ESP_OK;
    }
    if (launcher_app_open()) {
        ESP_LOGW(TAG, "destroy refused: an App is open");
        return ESP_ERR_INVALID_STATE;
    }

    /* Removing from the group and deleting the root releases every
     * child card, label and status object in one step. */
    lv_group_remove_obj(s_root);
    lv_obj_delete(s_root);

    s_root = NULL;
    s_hint = NULL;
    s_page = NULL;
    s_empty = NULL;
    s_focus = 0;
    for (size_t i = 0; i < XIAOMIAO_LAUNCHER_PER_PAGE; ++i) {
        s_slots[i].card = NULL;
        s_slots[i].icon = NULL;
        s_slots[i].placeholder = NULL;
        s_slots[i].name = NULL;
    }
    return ESP_OK;
}

size_t xiaomiao_launcher_focused_index(void)
{
    if (s_root == NULL) {
        return 0;
    }
    return s_focus;
}

size_t xiaomiao_launcher_page_index(void)
{
    if (s_root == NULL) {
        return 0;
    }
    return s_focus / XIAOMIAO_LAUNCHER_PER_PAGE;
}
