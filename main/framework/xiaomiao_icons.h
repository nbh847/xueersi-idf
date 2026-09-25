/*
 * Built-in Launcher icon loading (goal node 15, must-complete item
 * "built-in icon loading with LVGL symbol / placeholder fallback").
 *
 * Icons are read-only Flash resources at asset:/icons/<app-id>.bin:
 * fixed 16 x 16 RGB565 raw bitmaps, exactly 512 bytes, byte order as
 * produced by the host export (little-endian, matching LVGL's
 * LV_COLOR_FORMAT_RGB565 on ESP32).
 *
 * Any failure (missing file, wrong size, unreadable content, no PSRAM)
 * returns false and the caller keeps the existing LVGL Symbol label or
 * placeholder bar. Icons never block the Launcher (master plan
 * decision 14). Not thread safe: call from the LVGL UI task only.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMIAO_ICON_SIZE_PX 16

/*
 * Load the icon registered for `app_id` (e.g. "tools") into a cached
 * descriptor owned by this module. On success `*out_dsc` stays valid for
 * the firmware lifetime and may be shared by multiple lv_image objects.
 * Returns false when no valid built-in icon exists; `*out_dsc` is then
 * untouched.
 */
bool xiaomiao_icons_get(const char *app_id, lv_image_dsc_t *out_dsc);

#ifdef __cplusplus
}
#endif
