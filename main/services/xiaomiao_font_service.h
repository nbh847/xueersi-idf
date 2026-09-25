/*
 * Font Service (goal node 15B).
 *
 * Sixth System Service. It loads the full GB2312 Chinese font pack
 * (private XMF1 format) from the built-in Flash assets partition
 * through the Assets Service, validates it completely (magic, schema,
 * lengths, offsets, strictly ascending codepoints, payload CRC32) and
 * exposes two callback-driven lv_font_t objects (12 and 16 px, A2
 * bitmaps) with a Montserrat fallback chain.
 *
 * Hard rules (goal node 15B runtime contract):
 *  - The production font path is fixed to asset:/fonts/xiaomiao-zh-cn.xmf.
 *    There is no sd:/ font path anywhere in the firmware.
 *  - Any validation or allocation failure makes the whole Chinese font
 *    unavailable: the Service degrades to English and the token
 *    functions keep returning stable Montserrat fonts, never NULL.
 *  - The sorted codepoint index and the whole validated bitmap pack
 *    (759,456 bytes) live in PSRAM only; internal DMA RAM is never
 *    touched by font data. Glyph fetches are pointer math into the
 *    resident image, so no flash read happens during rendering
 *    (revision 2026-09-23 after a device measurement of ~17 ms per
 *    cold seek through the original streaming+LRU path).
 *  - The glyph callbacks run only on the LVGL UI thread: no task, no
 *    queue, no timer, no mutex is created here. init() runs on the
 *    boot task before any Chinese LVGL object is created.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMIAO_FONT_ZH_12_PX 12
#define XIAOMIAO_FONT_ZH_16_PX 16
#define XIAOMIAO_FONT_MIN_GLYPHS 7445
#define XIAOMIAO_FONT_MAX_GLYPHS 8192

typedef enum {
    XIAOMIAO_FONT_UNINITIALIZED = 0,
    XIAOMIAO_FONT_READY,        /* Chinese font validated and usable  */
    XIAOMIAO_FONT_FALLBACK,     /* English/Montserrat degradation     */
} xiaomiao_font_state_t;

typedef struct {
    xiaomiao_font_state_t state;
    uint32_t glyph_count;
    uint32_t font_bytes;
    uint32_t init_time_ms;
    /* Since the PSRAM-resident revision: cache_hits counts glyph bitmap
     * fetches served from the resident image; cache_misses is kept for
     * ABI and stays 0 (there is no flash-backed cache anymore). */
    uint32_t cache_hits;
    uint32_t cache_misses;
    esp_err_t last_error;
} xiaomiao_font_snapshot_t;

/*
 * Open asset:/fonts/xiaomiao-zh-cn.xmf through the Assets Service,
 * validate the whole pack and build the PSRAM index. Idempotent.
 * Never fails the boot: a non-ESP_OK return only means the Service is
 * in FALLBACK state and the tokens serve Montserrat.
 */
esp_err_t xiaomiao_font_service_init(void);

bool xiaomiao_font_service_ready(void);
void xiaomiao_font_service_get_snapshot(xiaomiao_font_snapshot_t *snapshot);

/*
 * The two validated Chinese lv_font_t objects. They return NULL while
 * the Service is not READY; UI code must use the token functions below
 * instead of these directly.
 */
const lv_font_t *xiaomiao_font_zh_12(void);
const lv_font_t *xiaomiao_font_zh_16(void);

/*
 * Validation-only probe used by the self test and the Tools Assets
 * diagnostics: parses and fully validates the given XMF1 file under
 * asset:/ without changing the Service state. Returns ESP_OK only
 * when every header/offset/ordering/CRC check passes.
 */
esp_err_t xiaomiao_font_service_probe(const char *relative_path);

/* Glyph walk access for the self test and diagnostics. Returns 0 when
 * out of range or not READY. */
uint32_t xiaomiao_font_service_glyph_count(void);
uint16_t xiaomiao_font_service_codepoint_at(uint32_t glyph_id);

#ifdef __cplusplus
}
#endif
