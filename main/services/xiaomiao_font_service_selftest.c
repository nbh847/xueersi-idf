/*
 * Font Service self test implementation (goal node 15B, CP3).
 *
 * Walks the full codepoint table through the public callbacks, checks
 * the resident-fetch counters and determinism and the damage-injection
 * fixture, and measures the heap deltas around init. Runs
 * single-threaded from app_main.
 */

#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "services/xiaomiao_assets_service.h"
#include "services/xiaomiao_font_service.h"
#include "services/xiaomiao_font_service_selftest.h"
#include "framework/xiaomiao_fonts.h"

static const char *TAG = "font_st";

static int s_failures;

#define ST_CHECK(cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            s_failures++;                                                    \
            ESP_LOGE(TAG, "FAIL line %d: %s", __LINE__, #cond);              \
        }                                                                    \
    } while (0)

/* Renders one codepoint into a private A8 buffer through the public
 * LVGL font API. Returns false on any failure; on true, out_buf holds
 * the A8 pixels and out_stride is valid. */
static bool render_glyph(const lv_font_t *font, uint32_t letter, uint8_t *out_buf,
                         size_t out_cap, uint32_t *out_stride, uint32_t *out_px)
{
    lv_font_glyph_dsc_t dsc;
    if (!lv_font_get_glyph_dsc(font, &dsc, letter, 0)) {
        return false;
    }
    lv_draw_buf_t *draw_buf = lv_draw_buf_create(dsc.box_w, dsc.box_h, LV_COLOR_FORMAT_A8, 0);
    if (draw_buf == NULL) {
        return false;
    }
    const void *got = lv_font_get_glyph_bitmap(&dsc, draw_buf);
    bool ok = false;
    if (got == draw_buf &&
        (size_t)draw_buf->header.stride * draw_buf->header.h <= out_cap) {
        memcpy(out_buf, draw_buf->data, (size_t)draw_buf->header.stride * draw_buf->header.h);
        *out_stride = draw_buf->header.stride;
        *out_px = dsc.box_w;
        ok = true;
    }
    lv_draw_buf_destroy(draw_buf);
    return ok;
}

static bool buffer_has_ink(const uint8_t *buf, uint32_t stride, uint32_t px)
{
    for (uint32_t y = 0; y < px; y++) {
        for (uint32_t x = 0; x < px; x++) {
            if (buf[y * stride + x] != 0) {
                return true;
            }
        }
    }
    return false;
}

void xiaomiao_font_service_selftest_run(void)
{
    s_failures = 0;

    lv_init();

    ESP_LOGI(TAG, "step 1: assets prerequisite");
    ST_CHECK(xiaomiao_assets_service_init() == ESP_OK);
    xiaomiao_assets_snapshot_t asnap;
    xiaomiao_assets_get_snapshot(&asnap);
    ST_CHECK(asnap.state == XIAOMIAO_ASSETS_READY);

    ESP_LOGI(TAG, "step 2: font init with heap accounting");
    size_t free_internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_lvgl_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t internal_dma_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    esp_err_t err = xiaomiao_font_service_init();
    ST_CHECK(err == ESP_OK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FONT_SERVICE_SELF_TEST: FAIL (init %s)", esp_err_to_name(err));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    size_t free_internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t internal_dma_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_LOGI(TAG, "heap delta: internal=%d SPIRAM=%d internalDMA=%d (bytes, free before-after)",
             (int)(free_internal_after - free_internal_before),
             (int)(free_spiram_after - free_spiram_before),
             (int)(internal_dma_after - internal_dma_before));
    (void)free_lvgl_before;
    ST_CHECK(xiaomiao_font_service_ready());
    /* Second init returns the first result. */
    ST_CHECK(xiaomiao_font_service_init() == ESP_OK);

    xiaomiao_font_snapshot_t fsnap;
    xiaomiao_font_service_get_snapshot(&fsnap);
    ST_CHECK(fsnap.state == XIAOMIAO_FONT_READY);
    ST_CHECK(fsnap.glyph_count >= XIAOMIAO_FONT_MIN_GLYPHS);
    ST_CHECK(fsnap.font_bytes < (1024u * 1024u));
    ESP_LOGI(TAG, "font: %u glyphs, %" PRIu32 " bytes, init %" PRIu32 " ms",
             (unsigned)fsnap.glyph_count, fsnap.font_bytes, fsnap.init_time_ms);

    ESP_LOGI(TAG, "step 3: walk the whole codepoint table");
    const lv_font_t *f12 = xiaomiao_font_zh_12();
    const lv_font_t *f16 = xiaomiao_font_zh_16();
    ST_CHECK(f12 != NULL && f16 != NULL);
    ST_CHECK(xiaomiao_font_small() == f12);
    ST_CHECK(xiaomiao_font_body() == f16);
    ST_CHECK(xiaomiao_font_title() == f16);
    /* Placement invariant (device bug 2026-09-24: LVGL 9.5 puts the box
     * at line_top + (line_height - base_line) - box_h - ofs_y, so a
     * wrong ofs_y/base_line shifted every CJK glyph one cell above the
     * line). The full cell must start exactly at the line top. */
    lv_font_glyph_dsc_t pdsc;
    ST_CHECK(lv_font_get_glyph_dsc(f16, &pdsc, xiaomiao_font_service_codepoint_at(0), 0));
    ST_CHECK((int32_t)f16->line_height - (int32_t)f16->base_line - (int32_t)pdsc.box_h -
                 (int32_t)pdsc.ofs_y == 0);
    ST_CHECK(lv_font_get_glyph_dsc(f12, &pdsc, xiaomiao_font_service_codepoint_at(0), 0));
    ST_CHECK((int32_t)f12->line_height - (int32_t)f12->base_line - (int32_t)pdsc.box_h -
                 (int32_t)pdsc.ofs_y == 0);
    if (f12 == NULL || f16 == NULL) {
        ESP_LOGE(TAG, "FONT_SERVICE_SELF_TEST: FAIL (no fonts)");
        return;
    }

    uint8_t buf12[12 * 16]; /* stride-padded A8 rows for the 12 px tier */
    uint8_t buf16[16 * 16];
    uint8_t again[16 * 16];
    uint32_t gc = xiaomiao_font_service_glyph_count();
    uint32_t walk_failures = 0;
    for (uint32_t id = 0; id < gc; id++) {
        uint16_t cp = xiaomiao_font_service_codepoint_at(id);
        uint32_t stride = 0;
        uint32_t px = 0;
        if (cp == 0 ||
            !render_glyph(f12, cp, buf12, sizeof(buf12), &stride, &px) || px != 12 ||
            !render_glyph(f16, cp, buf16, sizeof(buf16), &stride, &px)) {
            walk_failures++;
        }
        /* The unbroken walk keeps the main task busy for seconds and
         * starves IDLE0 past the 5 s task watchdog (device finding
         * 2026-09-23). Yield every 32 glyphs; ~230 yields add well
         * under a second and keep the idle tasks fed. */
        if ((id & 31u) == 31u) {
            vTaskDelay(1);
        }
        /* No output between step 3 and walk made a slow walk
         * indistinguishable from a hang on device (2026-09-23). */
        if ((id % 500u) == 499u) {
            ESP_LOGI(TAG, "walk progress: %" PRIu32 "/%" PRIu32 " at %" PRIu32 " ms",
                     id + 1, gc, (uint32_t)(esp_timer_get_time() / 1000));
        }
    }
    ST_CHECK(walk_failures == 0);
    ESP_LOGI(TAG, "walk done: %" PRIu32 " codepoints, %u failures", gc, (unsigned)walk_failures);

    ESP_LOGI(TAG, "step 4: ink + determinism + resident fetch counters");
    uint32_t missing_ink = 0;
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t id = (gc - 1) * i / 199; /* uniform sample incl. both ends */
        uint16_t cp = xiaomiao_font_service_codepoint_at(id);
        uint32_t stride = 0;
        uint32_t px = 0;
        if (!render_glyph(f16, cp, buf16, sizeof(buf16), &stride, &px) ||
            !buffer_has_ink(buf16, stride, px)) {
            missing_ink++;
        }
    }
    ST_CHECK(missing_ink == 0);

    /* Repeated reads must be byte-identical (resident image path). */
    uint32_t state = 0x12345678u;
    uint32_t mismatch = 0;
    xiaomiao_font_service_get_snapshot(&fsnap);
    uint32_t hits_before = fsnap.cache_hits;
    for (int i = 0; i < 50; i++) {
        state = state * 1664525u + 1013904223u;
        uint32_t id = (state >> 16) % gc;
        uint16_t cp = xiaomiao_font_service_codepoint_at(id);
        uint32_t s1 = 0, p1 = 0, s2 = 0, p2 = 0;
        if (!render_glyph(f16, cp, buf16, sizeof(buf16), &s1, &p1) ||
            !render_glyph(f16, cp, again, sizeof(again), &s2, &p2) ||
            s1 != s2 || p1 != p2 || memcmp(buf16, again, (size_t)s1 * p1) != 0) {
            mismatch++;
        }
    }
    ST_CHECK(mismatch == 0);
    xiaomiao_font_service_get_snapshot(&fsnap);
    ST_CHECK(fsnap.cache_hits > hits_before);
    /* The PSRAM-resident revision (2026-09-23) has no flash-backed
     * cache, so every fetch is a hit and misses must stay 0. */
    ST_CHECK(fsnap.cache_misses == 0);
    ESP_LOGI(TAG, "fetches: hits=%u misses=%u",
             (unsigned)fsnap.cache_hits, (unsigned)fsnap.cache_misses);

    ESP_LOGI(TAG, "step 5: fallback and absence behaviour");
    /* ASCII is not in the pack: dsc lookup must fail on the Chinese
     * font and succeed through the fallback chain. */
    lv_font_glyph_dsc_t dsc;
    ST_CHECK(f12->get_glyph_dsc(f12, &dsc, 'A', 0) == false);
    ST_CHECK(lv_font_get_glyph_dsc(f12, &dsc, 'A', 0) == true);
    ST_CHECK(dsc.resolved_font == &lv_font_montserrat_12);
    ST_CHECK(xiaomiao_font_service_codepoint_at(gc) == 0);

    ESP_LOGI(TAG, "step 6: damaged fixture must be rejected");
    ST_CHECK(xiaomiao_font_service_probe("fixtures/invalid-font.xmf") == ESP_ERR_INVALID_CRC);
    ST_CHECK(xiaomiao_font_service_probe("fonts/xiaomiao-zh-cn.xmf") == ESP_OK);
    ST_CHECK(xiaomiao_font_service_probe("fonts/no-such-font.xmf") == ESP_ERR_NOT_FOUND);

    xiaomiao_font_service_get_snapshot(&fsnap);
    ST_CHECK(fsnap.state == XIAOMIAO_FONT_READY); /* probe never changes state */

    if (s_failures == 0) {
        ESP_LOGI(TAG, "FONT_SERVICE_SELF_TEST: PASS");
        return;
    }
    ESP_LOGE(TAG, "FONT_SERVICE_SELF_TEST: FAIL (%d checks)", s_failures);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
