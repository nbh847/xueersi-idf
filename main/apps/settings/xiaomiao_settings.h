/*
 * Settings App (design doc section 6, goal node 8).
 *
 * Fourth independent business App and the first one that makes the
 * Launcher paginate: five registered Apps fill the first page
 * (Games / PC Monitor / Tools / Settings) and push Hardware Test onto
 * the second page.
 *
 * The App delivers the UI skeleton only. It owns an in-App menu
 * (Wi-Fi / Display / Sound / System) and four read-only status pages;
 * the Settings Service, NVS persistence, Wi-Fi, display control and the
 * Audio Service belong to later nodes, so no page offers a control that
 * could not take effect or be stored (goal decisions 12 and 13).
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
