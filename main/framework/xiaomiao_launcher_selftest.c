#include "xiaomiao_launcher_selftest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "xiaomiao_app.h"
#include "xiaomiao_launcher.h"
#include "xiaomiao_navigation.h"

static const char TAG[] = "launcher_selftest";

#define XM_TEST_APP_COUNT 16
#define XM_MAIN_APP_COUNT 7
#define XM_TEST_ROUNDS    3

#define XM_STATUS_H 14

static bool s_app_slot;
static int s_open_count;
static int s_close_count;

static size_t s_screen_baseline;
static lv_obj_t *s_status;

#define XM_CHECK(cond, msg)                                                 \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ESP_LOGE(TAG, "LAUNCHER_SELF_TEST: FAIL %s (%s)", msg, #cond);  \
            abort();                                                        \
        }                                                                   \
    } while (0)

/* ---------------------------------------------------------------- Apps */

static void test_app_open(void)
{
    lv_obj_t *root = xiaomiao_navigation_app_root();
    XM_CHECK(root != NULL, "navigation app root in open");

    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, "test App\npress B to return");
    lv_obj_center(label);

    s_app_slot = true;
    s_open_count++;
}

static void test_app_close(void)
{
    XM_CHECK(xiaomiao_navigation_app_root() != NULL, "navigation app root in close");
    XM_CHECK(s_app_slot, "open ran before close");
    s_app_slot = false;
    s_close_count++;
}

/*
 * Parameter names must not match the struct field names: the field
 * selectors `.id`, `.name` and `.icon` contain identifier tokens that
 * the preprocessor would otherwise substitute.
 */
#define XM_TEST_APP_ENTRY(app_id, app_name, app_icon)                        \
    {                                                                        \
        .id = app_id, .name = app_name, .icon = app_icon, .init = NULL,       \
        .open = test_app_open, .close = test_app_close,                       \
    }

/*
 * Sixteen Apps cover the Registry capacity. The count boundary checks
 * register the first 0/1/2/3/4/5/7/16 of them; the main flow registers
 * the first seven so page 0 holds four entries and page 1 holds three
 * with the last item alone in its row. `self.app0` carries an icon so
 * the icon label path is exercised, and one long name exercises the
 * deterministic truncation next to the placeholder path.
 */
static const xiaomiao_app_t s_apps[XM_TEST_APP_COUNT] = {
    XM_TEST_APP_ENTRY("self.app0", "App 0", "*"),
    XM_TEST_APP_ENTRY("self.app1", "App 1", NULL),
    XM_TEST_APP_ENTRY("self.app2", "App 2", NULL),
    XM_TEST_APP_ENTRY("self.app3", "Long Application", NULL),
    XM_TEST_APP_ENTRY("self.app4", "App 4", NULL),
    XM_TEST_APP_ENTRY("self.app5", "App 5", NULL),
    XM_TEST_APP_ENTRY("self.app6", "App 6", NULL),
    XM_TEST_APP_ENTRY("self.app7", "App 7", NULL),
    XM_TEST_APP_ENTRY("self.app8", "App 8", NULL),
    XM_TEST_APP_ENTRY("self.app9", "App 9", NULL),
    XM_TEST_APP_ENTRY("self.app10", "App 10", NULL),
    XM_TEST_APP_ENTRY("self.app11", "App 11", NULL),
    XM_TEST_APP_ENTRY("self.app12", "App 12", NULL),
    XM_TEST_APP_ENTRY("self.app13", "App 13", NULL),
    XM_TEST_APP_ENTRY("self.app14", "App 14", NULL),
    XM_TEST_APP_ENTRY("self.app15", "App 15", NULL),
};

/* Registry counts required by goal checkpoint 2. */
static const size_t s_counts[] = { 0, 1, 2, 3, 4, 5, 7, 16 };

/*
 * Guided key path. Each step is the key the human must press and the
 * focus index / page that must be visible afterwards. The same table
 * drives the automatic assertions so the synthetic and the real key
 * path verify identical transitions.
 *
 * The path is left/right only: it starts and ends at index 0, walks to
 * the last entry through one page turn, and returns the same way, so a
 * single traversal covers the clamped start, the page turn in both
 * directions and the clamped end. The two vertical steps are kept on
 * purpose to assert that they change nothing (launcher L/R paging goal,
 * decision 7).
 */
typedef struct {
    uint32_t key;
    size_t focus;
    size_t page;
    const char *hint;
} xm_step_t;

static const xm_step_t s_steps[] = {
    { LV_KEY_LEFT,  0, 0, "LEFT at the start stays" },
    { LV_KEY_DOWN,  0, 0, "DOWN is ignored" },
    { LV_KEY_UP,    0, 0, "UP is ignored" },
    { LV_KEY_RIGHT, 1, 0, "RIGHT" },
    { LV_KEY_RIGHT, 2, 0, "RIGHT" },
    { LV_KEY_RIGHT, 3, 0, "RIGHT" },
    { LV_KEY_RIGHT, 4, 1, "RIGHT turns the page" },
    { LV_KEY_RIGHT, 5, 1, "RIGHT" },
    { LV_KEY_RIGHT, 6, 1, "RIGHT" },
    { LV_KEY_RIGHT, 6, 1, "RIGHT at the end stays" },
    { LV_KEY_LEFT,  5, 1, "LEFT" },
    { LV_KEY_LEFT,  4, 1, "LEFT" },
    { LV_KEY_LEFT,  3, 0, "LEFT turns the page back" },
    { LV_KEY_LEFT,  2, 0, "LEFT" },
    { LV_KEY_LEFT,  1, 0, "LEFT" },
    { LV_KEY_LEFT,  0, 0, "LEFT" },
};

#define XM_STEP_COUNT (sizeof(s_steps) / sizeof(s_steps[0]))

/* ------------------------------------------------------------- helpers */

static size_t focus_index(void)
{
    return xiaomiao_launcher_focused_index();
}

static size_t page_index(void)
{
    return xiaomiao_launcher_page_index();
}

static size_t screen_children(void)
{
    return lv_obj_get_child_count(lv_screen_active());
}

/* Send a key through the real group/keypad path. */
static void press(lv_group_t *group, uint32_t key)
{
    lv_group_send_data(group, key);
}

static void check_step(size_t step, bool guided)
{
    if (focus_index() == s_steps[step].focus && page_index() == s_steps[step].page) {
        return;
    }

    ESP_LOGE(TAG,
             "LAUNCHER_SELF_TEST: FAIL step %u (%s): focus=%u page=%u expected focus=%u page=%u",
             (unsigned)step, guided ? "key path" : "auto", (unsigned)focus_index(),
             (unsigned)page_index(), (unsigned)s_steps[step].focus,
             (unsigned)s_steps[step].page);
    abort();
}

/* --------------------------------------------------------- status strip */

static void status_set(const char *text)
{
    if (s_status != NULL) {
        lv_label_set_text(s_status, text);
    }
}

static void status_create(void)
{
    s_status = lv_label_create(lv_layer_top());
    lv_obj_set_style_bg_color(s_status, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_status, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_all(s_status, 1, 0);
    lv_obj_set_size(s_status, LV_PCT(100), XM_STATUS_H);
    lv_obj_set_pos(s_status, 0, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(s_status, "launcher self test");
}

/* ------------------------------------------------- automatic assertions */

/* Lifecycle error paths that do not need a created Launcher. */
static void check_static_state(void)
{
    XM_CHECK(xiaomiao_launcher_focused_index() == 0, "focus 0 before create");
    XM_CHECK(xiaomiao_launcher_page_index() == 0, "page 0 before create");
    XM_CHECK(xiaomiao_launcher_destroy() == ESP_OK, "destroy idempotent before create");
    XM_CHECK(xiaomiao_launcher_create(NULL) == ESP_ERR_INVALID_ARG,
             "create NULL group rejected");
    XM_CHECK(xiaomiao_launcher_focused_index() == 0, "focus 0 after NULL create");
    XM_CHECK(xiaomiao_launcher_page_index() == 0, "page 0 after NULL create");
}

/*
 * Goal checkpoint 2 count boundaries: 0, 1, 2, 3, 4, 5, 7 and 16 Apps.
 * For each count the Launcher must create and destroy cleanly, keep the
 * focus in range and put the last reachable entry on the correct page.
 */
static void check_counts(lv_group_t *group)
{
    const size_t count_count = sizeof(s_counts) / sizeof(s_counts[0]);

    for (size_t c = 0; c < count_count; ++c) {
        const size_t n = s_counts[c];

        XM_CHECK(xiaomiao_app_registry_reset() == ESP_OK, "count reset");
        for (size_t i = 0; i < n; ++i) {
            XM_CHECK(xiaomiao_app_registry_register(&s_apps[i]) == ESP_OK,
                     "count register app");
        }
        XM_CHECK(xiaomiao_app_registry_count() == n, "count matches registration");

        XM_CHECK(xiaomiao_launcher_create(group) == ESP_OK, "count create");
        XM_CHECK(screen_children() == s_screen_baseline + 1, "count launcher on screen");
        XM_CHECK(focus_index() == 0, "count initial focus 0");
        XM_CHECK(page_index() == 0, "count initial page 0");

        XM_CHECK(xiaomiao_launcher_create(group) == ESP_ERR_INVALID_STATE,
                 "count double create rejected");
        XM_CHECK(screen_children() == s_screen_baseline + 1, "count double create stable");

        if (n == 0) {
            /* Every key is a no-op and no Navigation root appears. */
            press(group, LV_KEY_LEFT);
            press(group, LV_KEY_RIGHT);
            press(group, LV_KEY_UP);
            press(group, LV_KEY_DOWN);
            press(group, LV_KEY_ENTER);
            press(group, LV_KEY_ESC);
            XM_CHECK(focus_index() == 0, "empty count focus stays 0");
            XM_CHECK(page_index() == 0, "empty count page stays 0");
            XM_CHECK(xiaomiao_navigation_current() == NULL, "empty count opened nothing");
            XM_CHECK(xiaomiao_navigation_app_root() == NULL,
                     "empty count created no app root");
            XM_CHECK(screen_children() == s_screen_baseline + 1, "empty count keys leaked");
        } else {
            /* Right walks the whole Registry and settles on the last
             * index; there is no vertical move any more. */
            for (size_t i = 1; i < n; ++i) {
                press(group, LV_KEY_RIGHT);
            }
            XM_CHECK(focus_index() == n - 1, "count reaches the last index");
            XM_CHECK(page_index() == (n - 1) / XIAOMIAO_LAUNCHER_PER_PAGE,
                     "count last index on the right page");

            /* Further moves must never leave the valid range. */
            press(group, LV_KEY_RIGHT);
            XM_CHECK(focus_index() == n - 1, "right at the end stays");
            press(group, LV_KEY_DOWN);
            press(group, LV_KEY_UP);
            XM_CHECK(focus_index() == n - 1, "vertical keys are ignored");

            if (n > 1) {
                press(group, LV_KEY_LEFT);
                XM_CHECK(focus_index() == n - 2, "left steps back one entry");
                press(group, LV_KEY_RIGHT);
                XM_CHECK(focus_index() == n - 1, "right returns to the end");
            }

            XM_CHECK(focus_index() < n, "count focus stays in range");
            XM_CHECK(page_index() == focus_index() / XIAOMIAO_LAUNCHER_PER_PAGE,
                     "count page matches focus");
        }

        XM_CHECK(xiaomiao_launcher_destroy() == ESP_OK, "count destroy");
        XM_CHECK(screen_children() == s_screen_baseline, "count released its root");
        XM_CHECK(xiaomiao_launcher_destroy() == ESP_OK, "count destroy idempotent");
    }
}

static void check_moves_auto(lv_group_t *group)
{
    XM_CHECK(focus_index() == 0, "auto moves start at 0");
    XM_CHECK(page_index() == 0, "auto moves start on page 0");

    /* Boundary moves that must keep the focus. */
    press(group, LV_KEY_LEFT);
    XM_CHECK(focus_index() == 0, "left at the start stays");
    press(group, LV_KEY_UP);
    XM_CHECK(focus_index() == 0, "up is ignored");
    press(group, LV_KEY_DOWN);
    XM_CHECK(focus_index() == 0, "down is ignored");

    for (size_t i = 0; i < XM_STEP_COUNT; ++i) {
        press(group, s_steps[i].key);
        check_step(i, false);
    }

    XM_CHECK(focus_index() == 0, "auto moves end at 0");
    XM_CHECK(page_index() == 0, "auto moves end on page 0");
}

static void check_open_failure_before_init(lv_group_t *group)
{
    XM_CHECK(xiaomiao_navigation_current() == NULL, "no app before failure check");
    XM_CHECK(focus_index() == 0, "focus 0 before failure check");

    /* Manager not initialized yet: Navigation must refuse the open. */
    press(group, LV_KEY_ENTER);

    XM_CHECK(xiaomiao_navigation_current() == NULL, "open before manager init failed");
    XM_CHECK(xiaomiao_navigation_app_root() == NULL, "failed open left no app root");
    XM_CHECK(screen_children() == s_screen_baseline + 1, "failed open leaked no object");
    XM_CHECK(focus_index() == 0, "failed open kept focus");
    XM_CHECK(page_index() == 0, "failed open kept page");
}

static void check_open_back_auto(lv_group_t *group)
{
    for (int round = 0; round < 2; ++round) {
        const int open_before = s_open_count;

        press(group, LV_KEY_ENTER);
        XM_CHECK(xiaomiao_navigation_current() == &s_apps[0], "auto open focused app");
        XM_CHECK(xiaomiao_navigation_app_root() != NULL, "auto open created app root");
        XM_CHECK(s_open_count == open_before + 1, "auto open callback ran once");
        XM_CHECK(screen_children() == s_screen_baseline + 2, "auto open added app root");
        XM_CHECK(focus_index() == 0, "auto open kept focus");
        XM_CHECK(page_index() == 0, "auto open kept page");

        /*
         * Arrow keys are ignored while an App is open. Right is used on
         * purpose: it would move the focus if the guard were missing,
         * while the vertical keys never move it any more (launcher L/R
         * paging goal, decision 8).
         */
        press(group, LV_KEY_RIGHT);
        XM_CHECK(focus_index() == 0, "arrow ignored while app open");

        /* destroy is refused while an App is open and changes nothing. */
        XM_CHECK(xiaomiao_launcher_destroy() == ESP_ERR_INVALID_STATE,
                 "destroy refused while app is open");
        XM_CHECK(xiaomiao_navigation_current() == &s_apps[0], "refused destroy kept app");
        XM_CHECK(screen_children() == s_screen_baseline + 2,
                 "refused destroy kept app root");
        XM_CHECK(focus_index() == 0, "refused destroy kept focus");
        XM_CHECK(page_index() == 0, "refused destroy kept page");

        press(group, LV_KEY_ESC);
        XM_CHECK(xiaomiao_navigation_current() == NULL, "auto back closed app");
        XM_CHECK(s_close_count == s_open_count, "auto close callback ran once");
        XM_CHECK(screen_children() == s_screen_baseline + 1, "auto back released app root");
        XM_CHECK(focus_index() == 0, "auto back kept focus");
        XM_CHECK(page_index() == 0, "auto back kept page");

        /* B on the root page is idempotent. */
        const int close_before = s_close_count;
        press(group, LV_KEY_ESC);
        XM_CHECK(s_close_count == close_before, "root B ran no callback");
        XM_CHECK(screen_children() == s_screen_baseline + 1, "root B changed no object");
        XM_CHECK(focus_index() == 0, "root B kept focus");
    }
}

/* ------------------------------------------------------- guided key path */

typedef enum {
    XM_PHASE_MOVE = 0,
    XM_PHASE_OPEN,
    XM_PHASE_CLOSE,
    XM_PHASE_IDEMPOTENT,
    XM_PHASE_DONE,
} xm_phase_t;

static size_t s_step;
static int s_rounds;
static size_t s_round_focus;
static size_t s_round_page;
static xm_phase_t s_phase;

static void guided_prompt(void)
{
    char buffer[64];

    switch (s_phase) {
    case XM_PHASE_MOVE:
        snprintf(buffer, sizeof(buffer), "%u/%u  press %s", (unsigned)(s_step + 1),
                 (unsigned)XM_STEP_COUNT, s_steps[s_step].hint);
        status_set(buffer);
        break;
    case XM_PHASE_OPEN:
        snprintf(buffer, sizeof(buffer), "round %u/%u  press A to open",
                 (unsigned)(s_rounds + 1), (unsigned)XM_TEST_ROUNDS);
        status_set(buffer);
        break;
    case XM_PHASE_CLOSE:
        status_set("press B to return");
        break;
    case XM_PHASE_IDEMPOTENT:
        status_set("press B again (idempotent)");
        break;
    case XM_PHASE_DONE:
        status_set("LAUNCHER_SELF_TEST: PASS");
        break;
    }
}

static void guided_round_begin(void)
{
    s_round_focus = focus_index();
    s_round_page = page_index();
    s_phase = XM_PHASE_OPEN;
}

static void guided_key(uint32_t key)
{
    switch (s_phase) {
    case XM_PHASE_MOVE:
        XM_CHECK(key == s_steps[s_step].key, "unexpected key in guided traversal");
        check_step(s_step, true);
        s_step++;
        if (s_step >= XM_STEP_COUNT) {
            XM_CHECK(focus_index() == 0, "traversal ends at index 0");
            XM_CHECK(page_index() == 0, "traversal ends on page 0");
            ESP_LOGI(TAG, "guided traversal done");
            guided_round_begin();
        }
        guided_prompt();
        break;

    case XM_PHASE_OPEN:
        XM_CHECK(key == LV_KEY_ENTER, "round expects A to open");
        XM_CHECK(xiaomiao_navigation_current() == &s_apps[s_round_focus],
                 "opened app matches the focused entry");
        XM_CHECK(xiaomiao_navigation_app_root() != NULL, "round open created app root");
        XM_CHECK(s_open_count == s_close_count + 1, "round open callback ran once");
        XM_CHECK(screen_children() == s_screen_baseline + 2, "round open added app root");
        s_phase = XM_PHASE_CLOSE;
        guided_prompt();
        break;

    case XM_PHASE_CLOSE:
        XM_CHECK(key == LV_KEY_ESC, "round expects B to return");
        XM_CHECK(xiaomiao_navigation_current() == NULL, "round back closed the app");
        XM_CHECK(s_close_count == s_open_count, "round close callback ran once");
        XM_CHECK(screen_children() == s_screen_baseline + 1, "round back released app root");
        XM_CHECK(focus_index() == s_round_focus, "round back kept focus");
        XM_CHECK(page_index() == s_round_page, "round back kept page");
        s_phase = XM_PHASE_IDEMPOTENT;
        guided_prompt();
        break;

    case XM_PHASE_IDEMPOTENT:
        XM_CHECK(key == LV_KEY_ESC, "round expects B for the idempotent check");
        XM_CHECK(xiaomiao_navigation_current() == NULL, "idempotent B stays on the root page");
        XM_CHECK(s_close_count == s_open_count, "idempotent B ran no callback");
        XM_CHECK(screen_children() == s_screen_baseline + 1, "idempotent B changed no object");
        XM_CHECK(focus_index() == s_round_focus, "idempotent B kept focus");
        s_rounds++;
        ESP_LOGI(TAG, "round %d/%d done", s_rounds, XM_TEST_ROUNDS);
        if (s_rounds >= XM_TEST_ROUNDS) {
            XM_CHECK(s_open_count == s_close_count, "every open was closed");
            s_phase = XM_PHASE_DONE;
            guided_prompt();
            ESP_LOGI(TAG, "LAUNCHER_SELF_TEST: PASS");
        } else {
            guided_round_begin();
            guided_prompt();
        }
        break;

    case XM_PHASE_DONE:
        break;
    }
}

static void selftest_key_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY || s_phase == XM_PHASE_DONE) {
        return;
    }
    guided_key(lv_event_get_key(event));
}

/* ------------------------------------------------------------------ run */

void xiaomiao_launcher_selftest_run(lv_group_t *group)
{
    ESP_LOGI(TAG, "Launcher self test start");
    XM_CHECK(group != NULL, "group provided");

    status_create();

    s_screen_baseline = screen_children();

    check_static_state();
    check_counts(group);

    /* Main flow: seven Apps -> page 0 holds four, page 1 holds three. */
    XM_CHECK(xiaomiao_app_registry_reset() == ESP_OK, "main registry reset");
    for (size_t i = 0; i < XM_MAIN_APP_COUNT; ++i) {
        XM_CHECK(xiaomiao_app_registry_register(&s_apps[i]) == ESP_OK,
                 "register main test app");
    }
    XM_CHECK(xiaomiao_app_registry_count() == XM_MAIN_APP_COUNT, "main apps registered");

    XM_CHECK(xiaomiao_launcher_create(group) == ESP_OK, "create launcher with apps");
    XM_CHECK(xiaomiao_launcher_create(group) == ESP_ERR_INVALID_STATE,
             "double create with apps rejected");
    XM_CHECK(screen_children() == s_screen_baseline + 1, "launcher root on screen");
    XM_CHECK(focus_index() == 0, "initial focus 0");
    XM_CHECK(page_index() == 0, "initial page 0");

    check_moves_auto(group);
    check_open_failure_before_init(group);

    XM_CHECK(xiaomiao_app_manager_init_all() == ESP_OK, "manager init");
    check_open_back_auto(group);

    /*
     * Attach the guided state machine to the focused launcher root. The
     * Launcher registered its own key callback during create, so it runs
     * first and this callback observes the already-updated state.
     */
    lv_obj_t *root = lv_group_get_focused(group);
    XM_CHECK(root != NULL, "launcher root is focused");
    lv_obj_add_event_cb(root, selftest_key_cb, LV_EVENT_KEY, NULL);

    s_step = 0;
    s_rounds = 0;
    s_phase = XM_PHASE_MOVE;
    guided_prompt();

    ESP_LOGI(TAG, "automatic checks passed, guided key path: %u steps + %d rounds",
             (unsigned)XM_STEP_COUNT, XM_TEST_ROUNDS);
}
