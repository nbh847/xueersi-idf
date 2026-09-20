/*
 * Games placeholder App (design doc sections 13 and 15, goal node 5).
 *
 * First independent business App: it only proves the directory layout,
 * the static description, the Navigation lifecycle and the Launcher
 * return path. No real game logic, no hardware and no extra task.
 *
 * The description returned here is a firmware-lifetime static object.
 * Callers must not modify or free it, and must not keep LVGL objects
 * from it: the placeholder page belongs to the Navigation content root
 * and is released when the App is closed.
 */

#pragma once

#include "framework/xiaomiao_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return the static Games App description; never NULL. */
const xiaomiao_app_t *xiaomiao_games_app(void);

#ifdef __cplusplus
}
#endif
