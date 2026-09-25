/*
 * Font Service implementation (goal node 15B, PSRAM-resident revision).
 *
 * XMF1 layout (frozen with tools/font-pack, goal node 15B):
 *   [Header 64B][Codepoints 2*gc][pad to 4][Bitmap12 gc*36][Bitmap16 gc*64]
 * Header fields are little endian:
 *   0x00 magic "XMF1" | 0x04 schema u16=1 | 0x06 header_size u16=64
 *   0x08 glyph_count | 0x0C codepoint_offset | 0x10 bitmap_12_offset
 *   0x14 bitmap_16_offset | 0x18 file_size | 0x1C payload_crc32 (zlib
 *   CRC32 over [codepoint_offset, file_size))
 *   0x20 px12 0x21 line_height_12 0x22 base_12 0x23 adv_12
 *   0x24 px16 0x25 line_height_16 0x26 base_16 0x27 adv_16
 *   0x28..0x3F reserved zeros
 * Glyph units: row-major A2, 4 px/byte, leftmost pixel in the top two
 * bits, ceil(px*2/8) bytes per row (3 for 12 px, 4 for 16 px).
 *
 * Device finding 2026-09-23: a full-table self test measured ~17 ms per
 * cold glyph through the original streaming+LRU path. Every random
 * SPIFFS seek rescans the object lookup area, which is unacceptable
 * for first-draw UI latency. The pack is now read once into a PSRAM
 * image during init (the same bytes the CRC scan already streams, so
 * the cost stays ~0.5 s) and glyph fetches are pointer math. The whole
 * pack is 759,456 bytes against a 4 MB PSRAM heap.
 */

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "framework/xiaomiao_fonts.h"
#include "services/xiaomiao_assets_service.h"
#include "services/xiaomiao_font_service.h"

static const char *TAG = "font";

#define FONT_FILE_PATH "fonts/xiaomiao-zh-cn.xmf"
#define FONT_HEADER_SIZE 64
#define FONT_UNIT_12 36
#define FONT_UNIT_16 64
/* Upper bound for any single asset file we are willing to hold in
 * PSRAM: the assets partition itself is 1.5 MB. */
#define FONT_MAX_FILE_BYTES 0x180000u

typedef struct {
    uint32_t glyph_count;
    uint32_t codepoint_offset;
    uint32_t bitmap_12_offset;
    uint32_t bitmap_16_offset;
    uint32_t file_size;
    uint32_t payload_crc32;
    uint8_t px12;
    uint8_t line_height_12;
    uint8_t base_12;
    uint8_t adv_12;
    uint8_t px16;
    uint8_t line_height_16;
    uint8_t base_16;
    uint8_t adv_16;
} xmf_info_t;

typedef struct {
    bool inited;
    xiaomiao_font_state_t state;
    esp_err_t last_error;
    uint32_t init_time_ms;
    /* bitmap fetches served from the PSRAM image; kept under the old
     * cache_* names for snapshot ABI. cache_misses stays 0 now that
     * there is no flash-backed cache. */
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint8_t *image; /* PSRAM, whole validated file */
    uint16_t *codepoints; /* PSRAM, aligned copy of the sorted table */
    xmf_info_t info;
} font_service_t;

static font_service_t s_fs;

static lv_font_t s_font12;
static lv_font_t s_font16;

static const uint8_t s_opa2_table[4] = {0, 85, 170, 255};

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

/* zlib-compatible CRC32 (reflected 0xEDB88320), no table so it costs no
 * extra RAM. */
static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static uint32_t align4(uint32_t v)
{
    return (v + 3u) & ~3u;
}

/*
 * Full validation of an in-memory image. When codepoints_out is
 * non-NULL the sorted index is copied into a fresh PSRAM buffer on
 * success. All arithmetic is done on the frozen layout so a corrupt
 * header can never drive reads past the buffer.
 */
static esp_err_t font_validate_image(const uint8_t *img, size_t len, xmf_info_t *info,
                                     uint16_t **codepoints_out)
{
    if (len < FONT_HEADER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (memcmp(img, "XMF1", 4) != 0 || rd_le16(&img[4]) != 1 ||
        rd_le16(&img[6]) != FONT_HEADER_SIZE) {
        return ESP_ERR_INVALID_CRC; /* magic/schema/header mismatch */
    }
    for (int i = 0x28; i < 0x40; i++) {
        if (img[i] != 0) {
            return ESP_ERR_INVALID_CRC; /* reserved must be zero */
        }
    }

    memset(info, 0, sizeof(*info));
    info->glyph_count = rd_le32(&img[8]);
    info->codepoint_offset = rd_le32(&img[12]);
    info->bitmap_12_offset = rd_le32(&img[16]);
    info->bitmap_16_offset = rd_le32(&img[20]);
    info->file_size = rd_le32(&img[24]);
    info->payload_crc32 = rd_le32(&img[28]);
    info->px12 = img[0x20];
    info->line_height_12 = img[0x21];
    info->base_12 = img[0x22];
    info->adv_12 = img[0x23];
    info->px16 = img[0x24];
    info->line_height_16 = img[0x25];
    info->base_16 = img[0x26];
    info->adv_16 = img[0x27];

    /* Structural sanity only here (codepoints are uint16 and strictly
     * ascending, so a larger table cannot exist).  The "complete GB2312"
     * policy (minimum glyph count, pixel tiers, metrics) is enforced
     * after the CRC check below, so corrupt fixtures are always
     * rejected with ESP_ERR_INVALID_CRC instead of a policy error. */
    const uint32_t gc = info->glyph_count;
    if (gc == 0 || gc > 0xFFFF) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Offsets must be exactly the frozen layout; uint64 math rules out
     * multiplication overflow. */
    const uint64_t cp_end = (uint64_t)FONT_HEADER_SIZE + (uint64_t)gc * 2;
    const uint64_t want_bm12 = align4((uint32_t)cp_end);
    const uint64_t want_bm16 = want_bm12 + (uint64_t)gc * FONT_UNIT_12;
    const uint64_t want_size = want_bm16 + (uint64_t)gc * FONT_UNIT_16;
    if (info->codepoint_offset != FONT_HEADER_SIZE || info->bitmap_12_offset != want_bm12 ||
        info->bitmap_16_offset != want_bm16 || info->file_size != want_size ||
        (size_t)want_size > len || info->file_size != len) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Codepoints: strictly ascending, non-zero. */
    uint16_t prev = 0;
    const uint8_t *cp_tbl = img + info->codepoint_offset;
    for (uint32_t i = 0; i < gc; i++) {
        uint16_t cp = rd_le16(&cp_tbl[i * 2]);
        /* prev starts at 0, so the very first entry must be > 0. */
        if (cp == 0 || cp <= prev) {
            return ESP_ERR_INVALID_CRC;
        }
        prev = cp;
    }

    /* Payload CRC32 over [codepoint_offset, file_size). */
    uint32_t crc = crc32_update(0, img + info->codepoint_offset, len - info->codepoint_offset);
    if (crc != info->payload_crc32) {
        return ESP_ERR_INVALID_CRC;
    }

    /* Content policy: a structurally valid pack must still carry the full
     * GB2312 table at the two frozen A2 tiers. */
    if (gc < XIAOMIAO_FONT_MIN_GLYPHS || gc > XIAOMIAO_FONT_MAX_GLYPHS) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (info->px12 != XIAOMIAO_FONT_ZH_12_PX || info->px16 != XIAOMIAO_FONT_ZH_16_PX) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (info->line_height_12 < info->base_12 || info->line_height_12 > 32 || info->adv_12 == 0 ||
        info->adv_12 > 32 || info->line_height_16 < info->base_16 || info->line_height_16 > 32 ||
        info->adv_16 == 0 || info->adv_16 > 32) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (codepoints_out != NULL) {
        uint16_t *cps = heap_caps_malloc((size_t)gc * sizeof(uint16_t),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (cps == NULL) {
            return ESP_ERR_NO_MEM;
        }
        for (uint32_t i = 0; i < gc; i++) {
            cps[i] = rd_le16(&cp_tbl[i * 2]);
        }
        *codepoints_out = cps;
    }
    return ESP_OK;
}

/* Read the whole file (position 0) into a fresh PSRAM buffer. */
static esp_err_t font_slurp(xiaomiao_asset_file_t *file, uint8_t **out_img, size_t *out_len)
{
    uint32_t size = 0;
    esp_err_t err = xiaomiao_asset_get_size(file, &size);
    if (err != ESP_OK) {
        return err;
    }
    if (size < FONT_HEADER_SIZE || size > FONT_MAX_FILE_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *img = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (img == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t got = 0;
    err = xiaomiao_asset_read(file, img, size, &got);
    if (err != ESP_OK || got != size) {
        free(img);
        return err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
    }
    *out_img = img;
    *out_len = size;
    return ESP_OK;
}

/************************** LVGL callbacks **************************/

static bool xmf_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                              uint32_t letter, uint32_t letter_next)
{
    (void)letter_next;
    if (s_fs.state != XIAOMIAO_FONT_READY || letter == 0 || letter > 0xFFFF) {
        return false;
    }
    if (font != &s_font12 && font != &s_font16) {
        return false;
    }
    const uint32_t gc = s_fs.info.glyph_count;
    uint32_t lo = 0;
    uint32_t hi = gc;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint16_t cp = s_fs.codepoints[mid];
        if (cp == (uint16_t)letter) {
            lo = mid;
            break;
        }
        if (cp < (uint16_t)letter) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= gc || s_fs.codepoints[lo] != (uint16_t)letter) {
        return false; /* handled by the Montserrat fallback */
    }

    const bool is12 = (font == &s_font12);
    const uint8_t px = is12 ? s_fs.info.px12 : s_fs.info.px16;
    const uint8_t base = is12 ? s_fs.info.base_12 : s_fs.info.base_16;
    const uint8_t adv = is12 ? s_fs.info.adv_12 : s_fs.info.adv_16;

    dsc_out->adv_w = adv;
    dsc_out->box_w = px;
    dsc_out->box_h = px;
    dsc_out->ofs_x = 0;
    /* LVGL 9.5 lv_draw_label.c places the box with
     * y1 = line_top + (line_height - base_line) - box_h - ofs_y, i.e.
     * ofs_y is the gap of the box BOTTOM above the baseline (negative
     * when the cell dips below it). The full cell has its baseline
     * `base` px from the top, so the bottom sits base - px above it.
     * The old LVGL 8 reading (ofs_y = top above baseline) shifted every
     * CJK glyph one cell height above the line (device, 2026-09-24). */
    dsc_out->ofs_y = (int16_t)((int16_t)base - (int16_t)px);
    dsc_out->stride = (px == 12) ? 3 : 4;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A2;
    dsc_out->is_placeholder = 0;
    dsc_out->gid.index = lo;
    return true;
}

static const void *xmf_get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf)
{
    if (s_fs.state != XIAOMIAO_FONT_READY || draw_buf == NULL || draw_buf->data == NULL ||
        draw_buf->header.stride == 0) {
        return NULL;
    }
    const lv_font_t *font = g_dsc->resolved_font;
    if (font != &s_font12 && font != &s_font16) {
        return NULL;
    }
    const bool is12 = (font == &s_font12);
    const uint8_t px = is12 ? s_fs.info.px12 : s_fs.info.px16;
    const uint32_t gid = g_dsc->gid.index;
    if (gid >= s_fs.info.glyph_count) {
        return NULL;
    }
    /* The image passed full validation, so the frozen layout guarantees
     * this unit is inside the buffer. */
    const uint8_t *a2 = is12
                            ? s_fs.image + s_fs.info.bitmap_12_offset + gid * FONT_UNIT_12
                            : s_fs.image + s_fs.info.bitmap_16_offset + gid * FONT_UNIT_16;
    s_fs.cache_hits++;

    const uint32_t stride_in = is12 ? 3 : 4;
    uint8_t *out = draw_buf->data;
    const uint32_t stride_out = draw_buf->header.stride;
    for (uint32_t y = 0; y < px; y++) {
        for (uint32_t x = 0; x < px; x++) {
            const uint8_t byte = a2[y * stride_in + (x >> 2)];
            const uint8_t level = (byte >> (6 - ((x & 3) * 2))) & 3u;
            out[y * stride_out + x] = s_opa2_table[level];
        }
    }
    return draw_buf;
}

/************************** Service lifecycle ***********************/

static void fonts_build(void)
{
    memset(&s_font12, 0, sizeof(s_font12));
    s_font12.get_glyph_dsc = xmf_get_glyph_dsc;
    s_font12.get_glyph_bitmap = xmf_get_glyph_bitmap;
    /* lv_font_t.base_line is the descender: baseline-to-line-bottom
     * distance, not the ascent stored in the XMF1 header. */
    s_font12.line_height = s_fs.info.line_height_12;
    s_font12.base_line = s_fs.info.line_height_12 - s_fs.info.base_12;
    s_font12.subpx = LV_FONT_SUBPX_NONE;
    s_font12.kerning = LV_FONT_KERNING_NONE;
    s_font12.underline_position = -2;
    s_font12.underline_thickness = 1;
    s_font12.fallback = &lv_font_montserrat_12;

    memset(&s_font16, 0, sizeof(s_font16));
    s_font16.get_glyph_dsc = xmf_get_glyph_dsc;
    s_font16.get_glyph_bitmap = xmf_get_glyph_bitmap;
    s_font16.line_height = s_fs.info.line_height_16;
    s_font16.base_line = s_fs.info.line_height_16 - s_fs.info.base_16;
    s_font16.subpx = LV_FONT_SUBPX_NONE;
    s_font16.kerning = LV_FONT_KERNING_NONE;
    s_font16.underline_position = -2;
    s_font16.underline_thickness = 1;
    s_font16.fallback = &lv_font_montserrat_14;
}

esp_err_t xiaomiao_font_service_init(void)
{
    if (s_fs.inited) {
        return s_fs.last_error;
    }
    s_fs.inited = true;

    const int64_t t0 = esp_timer_get_time();
    xiaomiao_asset_file_t *file = NULL;
    esp_err_t err = xiaomiao_asset_open(XIAOMIAO_ASSET_SOURCE_ASSET, FONT_FILE_PATH, &file);
    if (err != ESP_OK) {
        s_fs.state = XIAOMIAO_FONT_FALLBACK;
        s_fs.last_error = err;
        ESP_LOGW(TAG, "font pack %s unavailable: %s (0x%x), falling back to English",
                 FONT_FILE_PATH, esp_err_to_name(err), (unsigned)err);
        return err;
    }

    size_t len = 0;
    err = font_slurp(file, &s_fs.image, &len);
    if (err == ESP_OK) {
        err = font_validate_image(s_fs.image, len, &s_fs.info, &s_fs.codepoints);
    }
    xiaomiao_asset_close(file); /* the image is self-contained now */

    if (err != ESP_OK) {
        free(s_fs.image);
        s_fs.image = NULL;
        free(s_fs.codepoints);
        s_fs.codepoints = NULL;
        s_fs.state = XIAOMIAO_FONT_FALLBACK;
        s_fs.last_error = err;
        ESP_LOGW(TAG, "font pack rejected: %s (0x%x), falling back to English",
                 esp_err_to_name(err), (unsigned)err);
        return err;
    }

    fonts_build();
    s_fs.state = XIAOMIAO_FONT_READY;
    s_fs.init_time_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    ESP_LOGI(TAG, "font READY: %u glyphs, %u bytes resident, init %u ms",
             (unsigned)s_fs.info.glyph_count, (unsigned)s_fs.info.file_size,
             (unsigned)s_fs.init_time_ms);
    return ESP_OK;
}

bool xiaomiao_font_service_ready(void)
{
    return s_fs.state == XIAOMIAO_FONT_READY;
}

void xiaomiao_font_service_get_snapshot(xiaomiao_font_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    snapshot->state = s_fs.inited ? s_fs.state : XIAOMIAO_FONT_UNINITIALIZED;
    snapshot->glyph_count = s_fs.info.glyph_count;
    snapshot->font_bytes = s_fs.info.file_size;
    snapshot->init_time_ms = s_fs.init_time_ms;
    snapshot->cache_hits = s_fs.cache_hits;
    snapshot->cache_misses = s_fs.cache_misses;
    snapshot->last_error = s_fs.last_error;
}

const lv_font_t *xiaomiao_font_zh_12(void)
{
    return s_fs.state == XIAOMIAO_FONT_READY ? &s_font12 : NULL;
}

const lv_font_t *xiaomiao_font_zh_16(void)
{
    return s_fs.state == XIAOMIAO_FONT_READY ? &s_font16 : NULL;
}

const lv_font_t *xiaomiao_font_small(void)
{
    const lv_font_t *f = xiaomiao_font_zh_12();
    return f != NULL ? f : &lv_font_montserrat_12;
}

const lv_font_t *xiaomiao_font_body(void)
{
    const lv_font_t *f = xiaomiao_font_zh_16();
    return f != NULL ? f : &lv_font_montserrat_14;
}

const lv_font_t *xiaomiao_font_title(void)
{
    const lv_font_t *f = xiaomiao_font_zh_16();
    return f != NULL ? f : &lv_font_montserrat_14;
}

uint32_t xiaomiao_font_service_glyph_count(void)
{
    return s_fs.state == XIAOMIAO_FONT_READY ? s_fs.info.glyph_count : 0;
}

uint16_t xiaomiao_font_service_codepoint_at(uint32_t glyph_id)
{
    if (s_fs.state != XIAOMIAO_FONT_READY || glyph_id >= s_fs.info.glyph_count) {
        return 0;
    }
    return s_fs.codepoints[glyph_id];
}

esp_err_t xiaomiao_font_service_probe(const char *relative_path)
{
    if (relative_path == NULL || relative_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    xiaomiao_asset_file_t *file = NULL;
    esp_err_t err = xiaomiao_asset_open(XIAOMIAO_ASSET_SOURCE_ASSET, relative_path, &file);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t *img = NULL;
    size_t len = 0;
    xmf_info_t info;
    err = font_slurp(file, &img, &len);
    if (err == ESP_OK) {
        err = font_validate_image(img, len, &info, NULL);
    }
    free(img);
    xiaomiao_asset_close(file);
    return err;
}
