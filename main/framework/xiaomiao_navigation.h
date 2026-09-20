/*
 * Navigation layer on top of the App Manager (design doc sections 4, 12).
 *
 * Navigation owns exactly one LVGL object per open App: the App content
 * root. It creates the content root before the App `open` callback runs
 * and deletes it after the App `close` callback returned, so returning
 * from an App always leaves the active screen exactly as it was before
 * the App was opened.
 *
 * Ownership contract:
 * - The current App state stays in the App Manager. Navigation never
 *   keeps a second copy of it (goal decision 2).
 * - The App content root is created by Navigation on the active screen
 *   and covers it. Apps only create child objects under it; they must
 *   not create, switch or delete global screens (goal decision 4).
 * - The App `close` callback stops timers, events and non-LVGL
 *   resources, but must not delete the content root. Navigation deletes
 *   it after the callback returned; deleting the parent releases all
 *   remaining children (goal decision 5).
 * - Only one App can be open at a time. Switching Apps requires going
 *   back to the root page first (goal decision 7).
 *
 * Threading: Navigation is not thread safe (goal decision 9). Call all
 * functions from the existing LVGL task only.
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#include "xiaomiao_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open the App with the given `id` through the App Manager and provide
 * it the App content root (xiaomiao_navigation_app_root()) for its
 * `open` callback.
 *
 * Returns:
 * - ESP_OK: App opened, content root created. The App `open` callback
 *   ran once and could use the content root.
 * - ESP_ERR_INVALID_ARG: `app_id` is NULL or empty. No object created.
 * - ESP_ERR_INVALID_STATE: the App Manager is not initialized yet, or
 *   another App is already open. The previous state (root page or
 *   current App) is unchanged and no object is left behind.
 * - ESP_ERR_NOT_FOUND: no registered App matches `app_id`. No object
 *   is left behind.
 * - ESP_ERR_NO_MEM: the content root object could not be created. The
 *   App is not opened and the state stays on the root page.
 */
esp_err_t xiaomiao_navigation_open(const char *app_id);

/*
 * Close the current App and return to the root page. Runs the App
 * `close` callback first, then deletes the content root together with
 * all remaining child objects.
 *
 * Calling this while no App is open (on the root page) is idempotent:
 * it returns ESP_OK without running any callback and without creating
 * or deleting any object (goal decision 8).
 *
 * Returns:
 * - ESP_OK: closed, or nothing to close.
 */
esp_err_t xiaomiao_navigation_back(void);

/*
 * Return the current App, or NULL when Navigation is on the root page.
 * The pointer is the one managed by the App Manager.
 */
const xiaomiao_app_t *xiaomiao_navigation_current(void);

/*
 * Return the content root of the currently open App, or NULL when
 * Navigation is on the root page. The root object is owned by
 * Navigation and stays valid until the App is closed.
 *
 * In stable states outside a synchronous lifecycle transition, this is
 * non-NULL if and only if xiaomiao_navigation_current() is non-NULL.
 * During the App `open` callback, the root is already available while
 * the Manager publishes the current App only after that callback returns.
 */
lv_obj_t *xiaomiao_navigation_app_root(void);

#ifdef __cplusplus
}
#endif
