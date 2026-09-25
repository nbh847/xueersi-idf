/*
 * UI font tokens (goal node 15B/15C).
 *
 * Single entry point for all UI text sizing. The tokens never return
 * NULL: while the Chinese font is not READY they fall back to the
 * closest compiled-in Montserrat so English strings are always drawn
 * with glyphs that exist. The implementations live in
 * services/xiaomiao_font_service.c.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* small: compact lists, hints and status lines (zh 12 px). */
const lv_font_t *xiaomiao_font_small(void);
/* body: main content and buttons (zh 16 px). */
const lv_font_t *xiaomiao_font_body(void);
/* title: headers (zh 16 px; identical to body until a heavier cut exists). */
const lv_font_t *xiaomiao_font_title(void);

#ifdef __cplusplus
}
#endif
