#include <stdbool.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "xiaomiao_app.h"
#include "xiaomiao_navigation.h"
#include "xiaomiao_navigation_selftest.h"

static const char TAG[] = "nav_selftest";

#define NAV_SELF_TEST_APP_ID      "self.nav"
#define NAV_SELF_TEST_ROUNDS      3
#define NAV_SELF_TEST_UI_CHILDREN 4

/*
 * Non-LVGL resource slot: acquired in the test App `open`, released in
 * `close`. Verifies that close runs and that open/close stay paired.
 */
static bool s_slot_in_use;

static int s_open_count;
static int s_close_count;

static size_t s_screen_baseline;
static int s_rounds_done;
static int s_idempotent_checks;
static bool s_await_idempotent;
static bool s_keypath_done;

static lv_obj_t *s_root_label;

#define NAV_CHECK(cond, msg)                                             \
    do {                                                                 \
        if (!(cond)) {                                                   \
            ESP_LOGE(TAG, "NAVIGATION_SELF_TEST: FAIL %s (%s)", msg,     \
                     #cond);                                             \
            abort();                                                     \
        }                                                                \
    } while (0)

static size_t screen_child_count(void)
{
    return lv_obj_get_child_count(lv_screen_active());
}

static void test_app_open(void)
{
    lv_obj_t *root = xiaomiao_navigation_app_root();
    NAV_CHECK(root != NULL, "app root available in open");
    NAV_CHECK(lv_obj_get_child_count(root) == 0, "app root starts empty");

    for (int i = 0; i < NAV_SELF_TEST_UI_CHILDREN; ++i) {
        lv_obj_t *label = lv_label_create(root);
        lv_label_set_text_fmt(label, "nav test child %d", i);
    }

    s_open_count++;
    s_slot_in_use = true;
}

static void test_app_close(void)
{
    NAV_CHECK(xiaomiao_navigation_app_root() != NULL, "app root alive in close");
    NAV_CHECK(s_slot_in_use, "slot acquired in open before close");
    s_slot_in_use = false;
    s_close_count++;
}

static const xiaomiao_app_t s_test_app = {
    .id = NAV_SELF_TEST_APP_ID,
    .name = "NavSelfTest",
    .icon = NULL,
    .init = NULL,
    .open = test_app_open,
    .close = test_app_close,
};

/* Error paths that must hold before the App Manager is initialized. */
static void check_errors_before_init(void)
{
    esp_err_t err;

    NAV_CHECK(xiaomiao_navigation_current() == NULL, "current empty before init");
    NAV_CHECK(xiaomiao_navigation_app_root() == NULL, "no app root before init");
    s_screen_baseline = screen_child_count();

    err = xiaomiao_navigation_open(NULL);
    NAV_CHECK(err == ESP_ERR_INVALID_ARG, "open NULL rejected");
    err = xiaomiao_navigation_open("");
    NAV_CHECK(err == ESP_ERR_INVALID_ARG, "open empty rejected");
    err = xiaomiao_navigation_open(NAV_SELF_TEST_APP_ID);
    NAV_CHECK(err == ESP_ERR_INVALID_STATE, "open before init rejected");

    NAV_CHECK(xiaomiao_navigation_current() == NULL, "no current after failed opens");
    NAV_CHECK(xiaomiao_navigation_app_root() == NULL, "no app root after failed opens");
    NAV_CHECK(screen_child_count() == s_screen_baseline, "no object leaked before init");
}

/* One full automatic open/back cycle plus error and idempotence paths. */
static void check_open_back_cycle(void)
{
    const size_t baseline = s_screen_baseline;
    esp_err_t err;

    err = xiaomiao_navigation_open("self.missing");
    NAV_CHECK(err == ESP_ERR_NOT_FOUND, "open unknown id rejected");
    NAV_CHECK(screen_child_count() == baseline, "no leak after not found");
    NAV_CHECK(xiaomiao_navigation_app_root() == NULL, "no app root after not found");

    err = xiaomiao_navigation_open(NAV_SELF_TEST_APP_ID);
    NAV_CHECK(err == ESP_OK, "open test app");
    NAV_CHECK(xiaomiao_navigation_current() == &s_test_app, "current is test app");
    NAV_CHECK(xiaomiao_navigation_app_root() != NULL, "app root created");
    NAV_CHECK(lv_obj_get_child_count(xiaomiao_navigation_app_root()) ==
                  NAV_SELF_TEST_UI_CHILDREN,
              "open created ui children");
    NAV_CHECK(s_open_count == s_close_count + 1, "open callback ran once");

    err = xiaomiao_navigation_open(NAV_SELF_TEST_APP_ID);
    NAV_CHECK(err == ESP_ERR_INVALID_STATE, "conflicting open rejected");
    NAV_CHECK(xiaomiao_navigation_current() == &s_test_app, "current unchanged after conflict");
    NAV_CHECK(xiaomiao_navigation_app_root() != NULL, "app root kept after conflict");
    NAV_CHECK(s_open_count == s_close_count + 1, "open callback not re-run on conflict");

    err = xiaomiao_navigation_back();
    NAV_CHECK(err == ESP_OK, "back to root");
    NAV_CHECK(s_close_count == s_open_count, "close callback ran once");
    NAV_CHECK(xiaomiao_navigation_current() == NULL, "current cleared after back");
    NAV_CHECK(xiaomiao_navigation_app_root() == NULL, "app root deleted after back");
    NAV_CHECK(screen_child_count() == baseline, "screen restored to baseline");

    err = xiaomiao_navigation_back();
    NAV_CHECK(err == ESP_OK, "back idempotent at root");
    NAV_CHECK(s_close_count == s_open_count, "close not re-run on idempotent back");
    NAV_CHECK(screen_child_count() == baseline, "baseline unchanged after idempotent back");
}

static void root_label_show(const char *text)
{
    lv_label_set_text(s_root_label, text);
}

static void open_test_app_round(void)
{
    const int round = s_rounds_done + 1;
    const int open_before = s_open_count;
    esp_err_t err = xiaomiao_navigation_open(NAV_SELF_TEST_APP_ID);
    NAV_CHECK(err == ESP_OK, "key path open");
    NAV_CHECK(s_open_count == open_before + 1, "key path open callback ran");
    NAV_CHECK(xiaomiao_navigation_current() == &s_test_app, "key path current set");
    NAV_CHECK(lv_obj_get_child_count(xiaomiao_navigation_app_root()) ==
                  NAV_SELF_TEST_UI_CHILDREN,
              "key path ui children created");
    NAV_CHECK(screen_child_count() == s_screen_baseline + 1, "key path screen has app root");
    root_label_show("press B to close");
    ESP_LOGI(TAG, "round %d/%d: test App opened", round, NAV_SELF_TEST_ROUNDS);
}

static void close_test_app_round(void)
{
    const int close_before = s_close_count;
    esp_err_t err = xiaomiao_navigation_back();
    NAV_CHECK(err == ESP_OK, "key path back");
    NAV_CHECK(s_close_count == close_before + 1, "key path close callback ran");
    NAV_CHECK(xiaomiao_navigation_current() == NULL, "key path current cleared");
    NAV_CHECK(xiaomiao_navigation_app_root() == NULL, "key path app root deleted");
    NAV_CHECK(screen_child_count() == s_screen_baseline, "key path screen restored");
    s_await_idempotent = true;
    root_label_show("press B to verify idempotent back");
    ESP_LOGI(TAG, "round %d/%d: test App closed", s_rounds_done + 1, NAV_SELF_TEST_ROUNDS);
}

static void verify_idempotent_back(void)
{
    esp_err_t err = xiaomiao_navigation_back();
    NAV_CHECK(err == ESP_OK, "root back idempotent");
    NAV_CHECK(xiaomiao_navigation_current() == NULL, "idempotent back keeps root page");
    NAV_CHECK(xiaomiao_navigation_app_root() == NULL, "idempotent back keeps no app root");
    NAV_CHECK(screen_child_count() == s_screen_baseline, "idempotent back keeps baseline");

    s_idempotent_checks++;
    s_await_idempotent = false;
    s_rounds_done++;

    if (s_rounds_done >= NAV_SELF_TEST_ROUNDS) {
        s_keypath_done = true;
        root_label_show("NAVIGATION_SELF_TEST: PASS\n(test finished)");
        ESP_LOGI(TAG, "NAVIGATION_SELF_TEST: PASS");
        return;
    }

    root_label_show("round done: press A to open");
    ESP_LOGI(TAG, "round %d/%d done, %d idempotent checks passed",
             s_rounds_done, NAV_SELF_TEST_ROUNDS, s_idempotent_checks);
}

static void selftest_key_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY || s_keypath_done) {
        return;
    }

    const uint32_t key = lv_event_get_key(e);

    if (xiaomiao_navigation_current() != NULL) {
        if (key == LV_KEY_ENTER) {
            esp_err_t err = xiaomiao_navigation_open(NAV_SELF_TEST_APP_ID);
            NAV_CHECK(err == ESP_ERR_INVALID_STATE, "in-app open rejected");
            ESP_LOGI(TAG, "in-app open rejected as expected");
        }
        else if (key == LV_KEY_ESC) {
            close_test_app_round();
        }
        else {
            ESP_LOGW(TAG, "press A or B inside the test App");
        }
        return;
    }

    if (s_await_idempotent) {
        if (key == LV_KEY_ESC) {
            verify_idempotent_back();
        }
        else {
            ESP_LOGW(TAG, "press B to verify idempotent back");
        }
        return;
    }

    if (key == LV_KEY_ENTER) {
        open_test_app_round();
    }
    else if (key == LV_KEY_ESC) {
        ESP_LOGW(TAG, "already on the root page, press A to open the test App");
    }
    else {
        ESP_LOGW(TAG, "press A to open the test App");
    }
}

static void build_root_ui(lv_group_t *group)
{
    lv_obj_t *panel = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101018), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(group, panel);
    lv_group_focus_obj(panel);
    lv_obj_add_event_cb(panel, selftest_key_cb, LV_EVENT_KEY, NULL);

    s_root_label = lv_label_create(panel);
    lv_label_set_text(s_root_label, "NAV SELF TEST\nround 1: press A to open");
    lv_obj_center(s_root_label);

    s_screen_baseline = screen_child_count();
    NAV_CHECK(s_screen_baseline > 0, "root page built");
}

void xiaomiao_navigation_selftest_run(lv_group_t *group)
{
    ESP_LOGI(TAG, "Navigation self test start");

    check_errors_before_init();

    esp_err_t err = xiaomiao_app_registry_register(&s_test_app);
    NAV_CHECK(err == ESP_OK, "register test app");
    err = xiaomiao_app_manager_init_all();
    NAV_CHECK(err == ESP_OK, "init all");

    check_open_back_cycle();
    check_open_back_cycle();

    build_root_ui(group);

    ESP_LOGI(TAG, "automatic checks passed, run %d A/B rounds on the device",
             NAV_SELF_TEST_ROUNDS);
}