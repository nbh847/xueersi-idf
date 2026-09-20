/*
 * Xiaomiao Launcher (design doc section 4, goal node 3).
 *
 * The Launcher is the root page on top of Navigation. It builds a fixed
 * 2 x 2 entry grid from the App Registry, moves focus with the left and
 * right keys, opens the focused App with A through Navigation and keeps
 * its page and focus when the App returns.
 *
 * Navigation model (launcher L/R paging goal, decisions 1 to 3):
 * - The Registry is read as one left-to-right sequence, and `<-` / `->`
 *   step one entry forward or back. The four slots of a page are
 *   visited in row-major order, so stepping past the fourth one turns
 *   the page.
 * - Both ends clamp instead of wrapping.
 * - `^` / `v` are not navigation keys and change nothing.
 *
 * Ownership and layout contract:
 * - The Launcher does not keep a copy of the App list. It reads App
 *   descriptions from the Registry in registration order (goal decision
 *   2). The Registry is fixed while the Launcher is alive.
 * - Every Launcher card, label and status object is a child of a single
 *   Launcher root created on the active screen. Destroying that root
 *   releases all children (goal decision 12).
 * - The Launcher only stores its own root, group, focused index and the
 *   page objects it created. The current App state stays in the App
 *   Manager and is only read through Navigation (goal decision 4).
 *
 * Threading: not thread safe (goal decision 12). Call every function
 * from the existing LVGL task only.
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed grid: two columns, two rows, four entries per page. */
#define XIAOMIAO_LAUNCHER_COLUMNS  2
#define XIAOMIAO_LAUNCHER_ROWS     2
#define XIAOMIAO_LAUNCHER_PER_PAGE \
    (XIAOMIAO_LAUNCHER_COLUMNS * XIAOMIAO_LAUNCHER_ROWS)

/*
 * Create the Launcher on the active screen and give it `group` so its
 * root object receives key events.
 *
 * Returns:
 * - ESP_OK: created. With an empty Registry the Launcher shows its empty
 *   state and still returns ESP_OK.
 * - ESP_ERR_INVALID_ARG: `group` is NULL. No object is created.
 * - ESP_ERR_INVALID_STATE: a Launcher already exists. The existing
 *   objects, focus and page are unchanged.
 * - ESP_ERR_NO_MEM: the root object could not be created.
 */
esp_err_t xiaomiao_launcher_create(lv_group_t *group);

/*
 * Destroy the Launcher root together with every child object and remove
 * it from its group.
 *
 * Returns:
 * - ESP_OK: destroyed, or nothing to destroy (idempotent when not
 *   created).
 * - ESP_ERR_INVALID_STATE: an App is currently open. The Launcher and
 *   the App stay unchanged.
 */
esp_err_t xiaomiao_launcher_destroy(void);

/*
 * Focused Registry index, or 0 when the Launcher is not created or the
 * Registry is empty. Never returns an out-of-range index.
 */
size_t xiaomiao_launcher_focused_index(void);

/*
 * Page index (focused index / entries per page), or 0 when the Launcher
 * is not created or the Registry is empty.
 */
size_t xiaomiao_launcher_page_index(void);

#ifdef __cplusplus
}
#endif
