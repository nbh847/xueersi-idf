/*
 * Assets Service self test (goal node 15A, CP3).
 *
 * Drives only public interfaces. Success prints exactly one marker:
 *   ASSETS_SERVICE_SELF_TEST: PASS
 * Any failure prints the failed step and halts forever.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "services/xiaomiao_assets_service.h"
#include "services/xiaomiao_assets_service_selftest.h"

static const char *TAG = "assets_st";

static int s_failures;

#define ST_CHECK(cond)                                                      \
    do {                                                                    \
        if (!(cond)) {                                                      \
            s_failures++;                                                   \
            ESP_LOGE(TAG, "FAIL line %d: %s", __LINE__, #cond);             \
        }                                                                   \
    } while (0)

static void check_path(const char *path, esp_err_t expected)
{
    esp_err_t err = xiaomiao_assets_validate_path(path);
    if (expected == ESP_OK) {
        ST_CHECK(err == ESP_OK);
    } else {
        ST_CHECK(err == expected);
    }
}

static void test_path_rules(void)
{
    char too_long[XIAOMIAO_ASSETS_PATH_MAX + 16];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';

    char at_limit[XIAOMIAO_ASSETS_PATH_MAX + 1];
    memset(at_limit, 'b', XIAOMIAO_ASSETS_PATH_MAX);
    at_limit[XIAOMIAO_ASSETS_PATH_MAX] = '\0';

    /* Legal shapes. */
    check_path("manifest.txt", ESP_OK);
    check_path("fonts/xiaomiao-zh-cn.xmf", ESP_OK);
    check_path("music/raw/example.raw", ESP_OK);
    check_path(at_limit, ESP_OK);

    /* Illegal shapes (goal node 15A constraints 4). */
    check_path(NULL, ESP_ERR_INVALID_ARG);
    check_path("", ESP_ERR_INVALID_ARG);
    check_path(too_long, ESP_ERR_INVALID_SIZE);
    check_path("/assets/manifest.txt", ESP_ERR_INVALID_ARG);
    check_path("/abs", ESP_ERR_INVALID_ARG);
    check_path("..", ESP_ERR_INVALID_ARG);
    check_path("../escape", ESP_ERR_INVALID_ARG);
    check_path("a/../b", ESP_ERR_INVALID_ARG);
    check_path("a/..", ESP_ERR_INVALID_ARG);
    check_path("./a", ESP_ERR_INVALID_ARG);
    check_path("a//b", ESP_ERR_INVALID_ARG);
    check_path("a/", ESP_ERR_INVALID_ARG);
    check_path("a\\b", ESP_ERR_INVALID_ARG);
    check_path("C:\\windows", ESP_ERR_INVALID_ARG);
    check_path("asset:/fonts/x.xmf", ESP_ERR_INVALID_ARG);
    check_path("sd:/x", ESP_ERR_INVALID_ARG);
    check_path("a\tb", ESP_ERR_INVALID_ARG);
}

static void test_file_lifecycle(void)
{
    xiaomiao_asset_file_t *file = NULL;
    ST_CHECK(xiaomiao_asset_open(XIAOMIAO_ASSET_SOURCE_ASSET, "manifest.txt", &file) == ESP_OK);
    if (file == NULL) {
        return;
    }

    uint32_t size = 0;
    ST_CHECK(xiaomiao_asset_get_size(file, &size) == ESP_OK);
    ST_CHECK(size > 0);

    char first[64];
    char second[64];
    size_t got = 0;
    /* The manifest may outgrow one buffer at any time, so the tested
     * window is clamped: the 2026-09-23 device run proved that trusting
     * `size <= sizeof(first)` let fread write past the stack buffer once
     * manifest.txt passed 64 bytes. */
    size_t want = ((size_t)size <= sizeof(first)) ? (size_t)size : sizeof(first);
    ST_CHECK(xiaomiao_asset_read(file, first, want, &got) == ESP_OK);
    ST_CHECK(got == want);

    /* Short read returns the real count without failing: whatever is
     * left after the first window, capped by the buffer. */
    got = 0;
    size_t remaining = (size_t)size - want;
    ST_CHECK(xiaomiao_asset_read(file, second, sizeof(second), &got) == ESP_OK);
    ST_CHECK(got == ((remaining < sizeof(second)) ? remaining : sizeof(second)));

    ST_CHECK(xiaomiao_asset_seek(file, 0) == ESP_OK);
    got = 0;
    ST_CHECK(xiaomiao_asset_read(file, second, want, &got) == ESP_OK);
    ST_CHECK(got == want);
    ST_CHECK(memcmp(first, second, want) == 0);

    /* Seek past EOF then read yields zero bytes, not an error. */
    ST_CHECK(xiaomiao_asset_seek(file, size + 10) == ESP_OK);
    got = 9;
    ST_CHECK(xiaomiao_asset_read(file, second, sizeof(second), &got) == ESP_OK);
    ST_CHECK(got == 0);

    xiaomiao_asset_close(file);

    /* NULL-safety and missing files. */
    xiaomiao_asset_close(NULL);
    file = (xiaomiao_asset_file_t *)1;
    ST_CHECK(xiaomiao_asset_open(XIAOMIAO_ASSET_SOURCE_ASSET, "no-such-file.bin", &file) == ESP_ERR_NOT_FOUND);
    ST_CHECK(file == (xiaomiao_asset_file_t *)1); /* untouched on failure */
    file = NULL;
    ST_CHECK(xiaomiao_asset_open(XIAOMIAO_ASSET_SOURCE_ASSET, "../escape", &file) == ESP_ERR_INVALID_ARG);
    ST_CHECK(xiaomiao_asset_read(NULL, first, 4, &got) == ESP_ERR_INVALID_ARG);
    ST_CHECK(xiaomiao_asset_seek(NULL, 0) == ESP_ERR_INVALID_ARG);
    ST_CHECK(xiaomiao_asset_get_size(NULL, &size) == ESP_ERR_INVALID_ARG);
}

static void test_namespaces(void)
{
    /* Storage Service is never initialized in this build, so sd:/
     * must fail deterministically without starting any mount. */
    xiaomiao_asset_file_t *file = NULL;
    ST_CHECK(xiaomiao_asset_open(XIAOMIAO_ASSET_SOURCE_SD, "music/test.raw", &file) == ESP_ERR_INVALID_STATE);
    ST_CHECK(file == NULL);

    xiaomiao_asset_entry_t entries[8];
    int count = 0;
    ST_CHECK(xiaomiao_asset_list(XIAOMIAO_ASSET_SOURCE_SD, NULL, entries, 8, &count) == ESP_ERR_INVALID_STATE);

    /* Bounded root listing must find the manifest. */
    count = 0;
    ST_CHECK(xiaomiao_asset_list(XIAOMIAO_ASSET_SOURCE_ASSET, NULL, entries, 8, &count) == ESP_OK);
    ST_CHECK(count >= 1);
    bool found = false;
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, "manifest.txt") == 0) {
            found = true;
            ST_CHECK(!entries[i].is_dir);
            ST_CHECK(entries[i].size_bytes > 0);
        }
    }
    ST_CHECK(found);

    /* Truncation contract: max_entries=1 returns at most one entry. */
    count = 0;
    ST_CHECK(xiaomiao_asset_list(XIAOMIAO_ASSET_SOURCE_ASSET, NULL, entries, 1, &count) == ESP_OK);
    ST_CHECK(count == 1);

    int zero = 5;
    ST_CHECK(xiaomiao_asset_list(XIAOMIAO_ASSET_SOURCE_ASSET, NULL, entries, 0, &zero) == ESP_ERR_INVALID_ARG);
    count = 0;
    ST_CHECK(xiaomiao_asset_list(XIAOMIAO_ASSET_SOURCE_ASSET, "..", entries, 8, &count) == ESP_ERR_INVALID_ARG);
    count = 0;
    ST_CHECK(xiaomiao_asset_list(XIAOMIAO_ASSET_SOURCE_ASSET, "no-such-dir", entries, 8, &count) == ESP_ERR_NOT_FOUND);
}

static void test_snapshot_and_idempotence(void)
{
    xiaomiao_assets_snapshot_t snap;
    xiaomiao_assets_get_snapshot(&snap);
    ST_CHECK(snap.state == XIAOMIAO_ASSETS_READY);
    ST_CHECK(snap.mounted);
    ST_CHECK(snap.last_error == ESP_OK);
    ST_CHECK(snap.total_bytes > 0 && snap.total_bytes <= 0x180000);

    /* Second init returns the first result without remounting. */
    ST_CHECK(xiaomiao_assets_service_init() == ESP_OK);
    xiaomiao_assets_snapshot_t snap2;
    xiaomiao_assets_get_snapshot(&snap2);
    ST_CHECK(memcmp(&snap, &snap2, sizeof(snap)) == 0);
    xiaomiao_assets_get_snapshot(NULL); /* no-op */
}

void xiaomiao_assets_service_selftest_run(void)
{
    s_failures = 0;

    ESP_LOGI(TAG, "step 1: path rules");
    test_path_rules();

    ESP_LOGI(TAG, "step 2: init + snapshot (expect the flashed assets image)");
    ST_CHECK(xiaomiao_assets_service_init() == ESP_OK);
    test_snapshot_and_idempotence();

    ESP_LOGI(TAG, "step 3: file lifecycle");
    test_file_lifecycle();

    ESP_LOGI(TAG, "step 4: namespaces");
    test_namespaces();

    if (s_failures == 0) {
        ESP_LOGI(TAG, "ASSETS_SERVICE_SELF_TEST: PASS");
        return;
    }
    ESP_LOGE(TAG, "ASSETS_SERVICE_SELF_TEST: FAIL (%d checks)", s_failures);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
