/*
 * Settings App (design doc section 6, goal nodes 8 and 9, 2026-09-30
 * reorganization).
 *
 * Fourth independent business App and the first one that makes the
 * Launcher paginate: five registered Apps fill the first page
 * (PC Monitor / Tools / Games / Settings) and push Hardware Test onto
 * the second page.
 *
 * The App owns an in-App menu (Wi-Fi / Display / Sound / System). The
 * Wi-Fi status row opens the connection details page migrated from
 * Tools, and System is a four-entry submenu holding the migrated Tools
 * diagnostics plus the original Settings Service persistence page.
 * Display states the fixed-backlight hardware fact. No page offers a
 * control that could not take effect or be stored.
 *
 * The description returned here is a firmware-lifetime static object.
 * Callers must not modify or free it, and must not keep LVGL objects
 * from it: the pages belong to the Navigation content root and are
 * released when the App is closed.
 */

#pragma once

#include "framework/xiaomiao_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return the static Settings App description; never NULL. */
const xiaomiao_app_t *xiaomiao_settings_app(void);

#ifdef __cplusplus
}
#endif
