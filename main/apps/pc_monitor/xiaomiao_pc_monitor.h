/*
 * PC Monitor skeleton App (design doc sections 6 and 15, goal node 6).
 *
 * Second independent business App: it only proves the directory layout,
 * the static description, the Navigation lifecycle, the Launcher return
 * path and the three-entry focus order. It shows fixed "no data"
 * placeholders and never touches a data source, a Service or hardware.
 *
 * The description returned here is a firmware-lifetime static object.
 * Callers must not modify or free it, and must not keep LVGL objects
 * from it: the skeleton page belongs to the Navigation content root and
 * is released when the App is closed.
 */

#pragma once

#include "framework/xiaomiao_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return the static PC Monitor App description; never NULL. */
const xiaomiao_app_t *xiaomiao_pc_monitor_app(void);

#ifdef __cplusplus
}
#endif
