/*
 * Xiaomiao App Framework core interfaces (design doc section 5).
 *
 * The framework only provides App description, Registry and lifecycle
 * Manager. It owns no hardware, no LVGL objects and no dynamic memory.
 * Navigation, Launcher and App switching are out of scope for node 1.
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed static capacity, no heap allocation (goal decision 3). */
#define XIAOMIAO_APP_REGISTRY_CAPACITY 16

/*
 * Lifecycle state of the currently managed App.
 *
 * - XIAOMIAO_APP_STATE_STOPPED: no App is open (initial state, and the
 *   state after a successful close).
 * - XIAOMIAO_APP_STATE_RUNNING: an App has been opened and not closed.
 */
typedef enum {
    XIAOMIAO_APP_STATE_STOPPED = 0,
    XIAOMIAO_APP_STATE_RUNNING,
} xiaomiao_app_state_t;

/*
 * App description.
 *
 * Ownership: the Registry stores the pointer as-is and never copies the
 * struct or its strings. Callers must keep the description (including
 * `id` and `name`) valid for the whole firmware lifetime, typically by
 * defining it as a static const object.
 *
 * `id` and `name` must be non-NULL and non-empty. `icon` and all three
 * callbacks are optional; NULL callbacks are treated as no-ops by the
 * framework.
 */
typedef struct {
    const char *id;
    const char *name;
    const char *icon;

    /*
     * Called once per App by xiaomiao_app_manager_init_all(), before any
     * open. NULL is allowed and skipped.
     */
    void (*init)(void);

    /*
     * Called by xiaomiao_app_manager_open() when this App becomes the
     * current App. NULL is allowed and skipped.
     */
    void (*open)(void);

    /*
     * Called by xiaomiao_app_manager_close() for the current App. NULL
     * is allowed and skipped.
     */
    void (*close)(void);
} xiaomiao_app_t;

/*
 * Registry API.
 *
 * Not thread safe and no locking (goal decision 6): only call from the
 * startup path or the LVGL task. Registration order is the enumeration
 * order. Returned pointers stay valid for the framework lifetime.
 */

/* Remove all registered Apps. Intended for tests and pre-boot setup. */
esp_err_t xiaomiao_app_registry_reset(void);

/*
 * Register one App.
 *
 * Returns:
 * - ESP_OK: registered, appended after all previously registered Apps.
 * - ESP_ERR_INVALID_ARG: `app` is NULL, or `id`/`name` is NULL or empty.
 * - ESP_ERR_INVALID_STATE: an App with the same `id` already exists.
 * - ESP_ERR_NO_MEM: registry already holds XIAOMIAO_APP_REGISTRY_CAPACITY
 *   Apps. The rejected App is dropped; existing entries are untouched.
 */
esp_err_t xiaomiao_app_registry_register(const xiaomiao_app_t *app);

/* Number of currently registered Apps. */
size_t xiaomiao_app_registry_count(void);

/*
 * Return the App registered at `index` (0 = first registered), or NULL
 * when `index` is out of range.
 */
const xiaomiao_app_t *xiaomiao_app_registry_get_at(size_t index);

/*
 * Return the App whose `id` matches exactly, or NULL when not found or
 * when `id` is NULL/empty.
 */
const xiaomiao_app_t *xiaomiao_app_registry_find(const char *id);

/*
 * Manager API.
 *
 * Manages at most one current App and runs lifecycle callbacks in the
 * caller's context. Not thread safe (goal decision 6). `init_all` must
 * run before `open`; it is a one-shot operation and cannot be repeated.
 */

/*
 * Run the `init` callback of every registered App once, in registration
 * order.
 *
 * Returns:
 * - ESP_OK: all Apps initialized; init callbacks are never run again.
 * - ESP_ERR_INVALID_STATE: init_all was already called before.
 */
esp_err_t xiaomiao_app_manager_init_all(void);

/*
 * Open the App with the given `id` and make it the current App.
 *
 * Returns:
 * - ESP_OK: App opened (its `open` callback ran unless NULL), state is
 *   XIAOMIAO_APP_STATE_RUNNING.
 * - ESP_ERR_INVALID_ARG: `id` is NULL or empty.
 * - ESP_ERR_INVALID_STATE: init_all has not run yet, or another App is
 *   already open. No callback runs on this error.
 * - ESP_ERR_NOT_FOUND: no registered App matches `id`.
 */
esp_err_t xiaomiao_app_manager_open(const char *id);

/*
 * Close the current App and return to XIAOMIAO_APP_STATE_STOPPED. The
 * `close` callback runs unless NULL. Calling this with no current App
 * is idempotent and returns ESP_OK without running any callback.
 *
 * Returns:
 * - ESP_OK: closed, or nothing to close.
 */
esp_err_t xiaomiao_app_manager_close(void);

/*
 * Return the current App, or NULL when no App is open. The pointer is
 * the one passed to xiaomiao_app_registry_register().
 */
const xiaomiao_app_t *xiaomiao_app_manager_current(void);

#ifdef __cplusplus
}
#endif