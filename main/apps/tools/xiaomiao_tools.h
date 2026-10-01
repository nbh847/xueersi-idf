/*
 * Tools App (design doc section 6, goal node 7, 2026-09-30 reorganization).
 *
 * Business App in its transition state: the former Wi-Fi, System Info,
 * About and Assets diagnostics moved into the Settings App, and the
 * final Pomodoro entry is a separate goal. Until then the page shows
 * the localized "no tools" state only and B returns to the Launcher.
 * The App keeps its registration, icon and lifecycle, and owns its own
 * input focus through the LVGL default group.
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

/* Return the static Tools App description; never NULL. */
const xiaomiao_app_t *xiaomiao_tools_app(void);

#ifdef __cplusplus
}
#endif
