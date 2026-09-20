/*
 * Tools App (design doc section 6, goal node 7).
 *
 * Third independent business App and the first one with an in-App menu:
 * Wi-Fi, System Info and About. It shows real read-only system data,
 * states plainly that the Wi-Fi Service does not exist yet (node 10) and
 * owns its own input focus through the LVGL default group.
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
