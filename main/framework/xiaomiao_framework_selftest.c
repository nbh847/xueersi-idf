#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "xiaomiao_app.h"
#include "xiaomiao_framework_selftest.h"

static const char TAG[] = "fw_selftest";

typedef enum {
    EV_INIT_A,
    EV_INIT_B,
    EV_OPEN_A,
    EV_OPEN_B,
    EV_CLOSE_A,
    EV_CLOSE_B,
} test_event_t;

static int s_init_count[2];
static int s_open_count[2];
static int s_close_count[2];
static test_event_t s_events[16];
static size_t s_event_count;

static void push_event(test_event_t event)
{
    if (s_event_count < sizeof(s_events) / sizeof(s_events[0])) {
        s_events[s_event_count++] = event;
    }
}

static void test_init_a(void) { s_init_count[0]++; push_event(EV_INIT_A); }
static void test_open_a(void) { s_open_count[0]++; push_event(EV_OPEN_A); }
static void test_close_a(void) { s_close_count[0]++; push_event(EV_CLOSE_A); }
static void test_init_b(void) { s_init_count[1]++; push_event(EV_INIT_B); }
static void test_open_b(void) { s_open_count[1]++; push_event(EV_OPEN_B); }
static void test_close_b(void) { s_close_count[1]++; push_event(EV_CLOSE_B); }

static const xiaomiao_app_t s_test_app_a = {
    .id = "self.a",
    .name = "SelfTest A",
    .icon = NULL,
    .init = test_init_a,
    .open = test_open_a,
    .close = test_close_a,
};

static const xiaomiao_app_t s_test_app_b = {
    .id = "self.b",
    .name = "SelfTest B",
    .init = test_init_b,
    .open = test_open_b,
    .close = test_close_b,
};

#define TEST_FILLER_APP(n)                       \
    {                                            \
        .id = "self.fill." #n,                   \
        .name = "Filler " #n,                    \
        .init = NULL,                            \
        .open = NULL,                            \
        .close = NULL,                           \
    }

static const xiaomiao_app_t s_filler_apps[14] = {
    TEST_FILLER_APP(0),
    TEST_FILLER_APP(1),
    TEST_FILLER_APP(2),
    TEST_FILLER_APP(3),
    TEST_FILLER_APP(4),
    TEST_FILLER_APP(5),
    TEST_FILLER_APP(6),
    TEST_FILLER_APP(7),
    TEST_FILLER_APP(8),
    TEST_FILLER_APP(9),
    TEST_FILLER_APP(10),
    TEST_FILLER_APP(11),
    TEST_FILLER_APP(12),
    TEST_FILLER_APP(13),
};

#define SELFTEST_CHECK(cond, msg)                                        \
    do {                                                                 \
        if (!(cond)) {                                                   \
            ESP_LOGE(TAG, "APP_FRAMEWORK_SELF_TEST: FAIL %s (%s)", msg,  \
                     #cond);                                             \
            abort();                                                     \
        }                                                                \
    } while (0)

static void selftest_check_registry(void)
{
    const xiaomiao_app_t *app = NULL;
    esp_err_t err;

    SELFTEST_CHECK(xiaomiao_app_registry_count() == 0, "registry starts empty");

    err = xiaomiao_app_registry_register(NULL);
    SELFTEST_CHECK(err == ESP_ERR_INVALID_ARG, "register NULL rejected");

    static const xiaomiao_app_t app_empty_id = {.id = "", .name = "EmptyId"};
    err = xiaomiao_app_registry_register(&app_empty_id);
    SELFTEST_CHECK(err == ESP_ERR_INVALID_ARG, "empty id rejected");

    static const xiaomiao_app_t app_null_id = {.name = "NullId"};
    err = xiaomiao_app_registry_register(&app_null_id);
    SELFTEST_CHECK(err == ESP_ERR_INVALID_ARG, "NULL id rejected");

    static const xiaomiao_app_t app_null_name = {.id = "self.noname"};
    err = xiaomiao_app_registry_register(&app_null_name);
    SELFTEST_CHECK(err == ESP_ERR_INVALID_ARG, "NULL name rejected");

    static const xiaomiao_app_t app_empty_name = {.id = "self.noname2", .name = ""};
    err = xiaomiao_app_registry_register(&app_empty_name);
    SELFTEST_CHECK(err == ESP_ERR_INVALID_ARG, "empty name rejected");

    err = xiaomiao_app_registry_register(&s_test_app_a);
    SELFTEST_CHECK(err == ESP_OK, "register app A");
    err = xiaomiao_app_registry_register(&s_test_app_b);
    SELFTEST_CHECK(err == ESP_OK, "register app B");
    SELFTEST_CHECK(xiaomiao_app_registry_count() == 2, "count is 2");

    app = xiaomiao_app_registry_get_at(0);
    SELFTEST_CHECK(app == &s_test_app_a, "index 0 is app A");
    app = xiaomiao_app_registry_get_at(1);
    SELFTEST_CHECK(app == &s_test_app_b, "index 1 is app B");
    SELFTEST_CHECK(xiaomiao_app_registry_get_at(2) == NULL, "index 2 is NULL");

    app = xiaomiao_app_registry_find("self.a");
    SELFTEST_CHECK(app == &s_test_app_a, "find app A by id");
    app = xiaomiao_app_registry_find("self.b");
    SELFTEST_CHECK(app == &s_test_app_b, "find app B by id");
    SELFTEST_CHECK(xiaomiao_app_registry_find("self.missing") == NULL, "unknown id not found");
    SELFTEST_CHECK(xiaomiao_app_registry_find(NULL) == NULL, "NULL id not found");

    static const xiaomiao_app_t app_dup_id = {.id = "self.a", .name = "Duplicate"};
    err = xiaomiao_app_registry_register(&app_dup_id);
    SELFTEST_CHECK(err == ESP_ERR_INVALID_STATE, "duplicate id rejected");
    SELFTEST_CHECK(xiaomiao_app_registry_count() == 2, "count unchanged after duplicate");
}

static void selftest_check_registry_capacity(void)
{
    esp_err_t err;

    for (size_t i = 0; i < sizeof(s_filler_apps) / sizeof(s_filler_apps[0]); ++i) {
        err = xiaomiao_app_registry_register(&s_filler_apps[i]);
        SELFTEST_CHECK(err == ESP_OK, "register filler app");
    }
    SELFTEST_CHECK(xiaomiao_app_registry_count() == XIAOMIAO_APP_REGISTRY_CAPACITY,
                   "registry full");

    static const xiaomiao_app_t app_overflow = {.id = "self.overflow", .name = "Overflow"};
    err = xiaomiao_app_registry_register(&app_overflow);
    SELFTEST_CHECK(err == ESP_ERR_NO_MEM, "capacity overflow rejected");
    SELFTEST_CHECK(xiaomiao_app_registry_count() == XIAOMIAO_APP_REGISTRY_CAPACITY,
                   "count unchanged after overflow");

    err = xiaomiao_app_registry_reset();
    SELFTEST_CHECK(err == ESP_OK, "registry reset");
    SELFTEST_CHECK(xiaomiao_app_registry_count() == 0, "registry empty after reset");
    SELFTEST_CHECK(xiaomiao_app_registry_get_at(0) == NULL, "get_at empty after reset");
    SELFTEST_CHECK(xiaomiao_app_registry_find("self.a") == NULL, "find empty after reset");
}

static void selftest_check_manager_lifecycle(void)
{
    esp_err_t err;

    err = xiaomiao_app_registry_register(&s_test_app_a);
    SELFTEST_CHECK(err == ESP_OK, "re-register app A");
    err = xiaomiao_app_registry_register(&s_test_app_b);
    SELFTEST_CHECK(err == ESP_OK, "re-register app B");

    err = xiaomiao_app_manager_open("self.a");
    SELFTEST_CHECK(err == ESP_ERR_INVALID_STATE, "open before init rejected");
    SELFTEST_CHECK(xiaomiao_app_manager_current() == NULL, "no current before init");

    err = xiaomiao_app_manager_init_all();
    SELFTEST_CHECK(err == ESP_OK, "init_all");
    SELFTEST_CHECK(s_init_count[0] == 1 && s_init_count[1] == 1, "init ran once per app");
    SELFTEST_CHECK(s_event_count == 2 && s_events[0] == EV_INIT_A && s_events[1] == EV_INIT_B,
                   "init order A then B");

    err = xiaomiao_app_manager_init_all();
    SELFTEST_CHECK(err == ESP_ERR_INVALID_STATE, "second init_all rejected");
    SELFTEST_CHECK(s_init_count[0] == 1 && s_init_count[1] == 1, "init not re-run");

    err = xiaomiao_app_manager_open(NULL);
    SELFTEST_CHECK(err == ESP_ERR_INVALID_ARG, "open NULL id rejected");
    err = xiaomiao_app_manager_open("");
    SELFTEST_CHECK(err == ESP_ERR_INVALID_ARG, "open empty id rejected");
    err = xiaomiao_app_manager_open("self.missing");
    SELFTEST_CHECK(err == ESP_ERR_NOT_FOUND, "open unknown id rejected");
    SELFTEST_CHECK(xiaomiao_app_manager_current() == NULL, "no current after failed opens");

    err = xiaomiao_app_manager_open("self.b");
    SELFTEST_CHECK(err == ESP_OK, "open app B");
    SELFTEST_CHECK(xiaomiao_app_manager_current() == &s_test_app_b, "current is app B");
    SELFTEST_CHECK(s_open_count[1] == 1, "open B ran once");
    SELFTEST_CHECK(s_open_count[0] == 0, "A not opened yet");

    err = xiaomiao_app_manager_open("self.a");
    SELFTEST_CHECK(err == ESP_ERR_INVALID_STATE, "open conflict rejected");
    SELFTEST_CHECK(s_open_count[0] == 0, "A open callback not run on conflict");
    SELFTEST_CHECK(xiaomiao_app_manager_current() == &s_test_app_b, "current still app B");

    err = xiaomiao_app_manager_close();
    SELFTEST_CHECK(err == ESP_OK, "close app B");
    SELFTEST_CHECK(s_close_count[1] == 1, "close B ran once");
    SELFTEST_CHECK(xiaomiao_app_manager_current() == NULL, "no current after close");

    err = xiaomiao_app_manager_close();
    SELFTEST_CHECK(err == ESP_OK, "close without current idempotent");
    SELFTEST_CHECK(s_close_count[1] == 1, "close B not re-run");

    err = xiaomiao_app_manager_open("self.a");
    SELFTEST_CHECK(err == ESP_OK, "open app A");
    SELFTEST_CHECK(xiaomiao_app_manager_current() == &s_test_app_a, "current is app A");
    SELFTEST_CHECK(s_open_count[0] == 1, "open A ran once");

    err = xiaomiao_app_manager_close();
    SELFTEST_CHECK(err == ESP_OK, "close app A");
    SELFTEST_CHECK(s_close_count[0] == 1, "close A ran once");

    err = xiaomiao_app_manager_open("self.b");
    SELFTEST_CHECK(err == ESP_OK, "reopen app B after close");
    SELFTEST_CHECK(s_open_count[1] == 2, "open B ran twice");
    err = xiaomiao_app_manager_close();
    SELFTEST_CHECK(err == ESP_OK, "final close app B");
    SELFTEST_CHECK(s_close_count[1] == 2, "close B ran twice");
    SELFTEST_CHECK(xiaomiao_app_manager_current() == NULL, "final state has no current");

    {
        const test_event_t expected[] = {
            EV_INIT_A, EV_INIT_B,
            EV_OPEN_B, EV_CLOSE_B,
            EV_OPEN_A, EV_CLOSE_A,
            EV_OPEN_B, EV_CLOSE_B,
        };
        SELFTEST_CHECK(s_event_count == sizeof(expected) / sizeof(expected[0]),
                       "event count matches");
        SELFTEST_CHECK(memcmp(s_events, expected, sizeof(expected)) == 0,
                       "event order matches");
    }
}

void xiaomiao_framework_selftest_run(void)
{
    ESP_LOGI(TAG, "App framework self test start");

    selftest_check_registry();
    selftest_check_registry_capacity();
    selftest_check_manager_lifecycle();

    ESP_LOGI(TAG, "APP_FRAMEWORK_SELF_TEST: PASS");
}