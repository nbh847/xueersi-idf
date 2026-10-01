/*
 * Framework screen idle control (goal 20261001-1449).
 *
 * One global UI idle screen: after a configurable time without a key
 * press the display turns into a full-screen black overlay with the
 * confirmed little-TV doodle (blinking eyes, ear-like antennas, a
 * subtly moving mouth). Any of the six keys wakes the screen; the wake
 * key and every input up to the release of all keys is swallowed, so
 * no page action and no Hardware Test B gesture can fire.
 *
 * Boundaries (goal, "Input and page lifecycle"):
 * - The overlay lives on the LVGL top layer, never takes the group
 *   focus and never calls App open/close; the page underneath, its
 *   focus and the App lifecycle are untouched.
 * - All LVGL work happens in the UI thread: report_key() is called from
 *   the keypad indev read callback and poll() from the LVGL loop, both
 *   of which run inside lv_timer_handler(). No FreeRTOS task, lock or
 *   queue is created.
 * - Activity is only the debounced press/release of the six keys;
 *   background refreshes never postpone the timeout. Time is the 64-bit
 *   monotonic esp_timer clock.
 * - The backlight is wired to VCC: the feature only covers the screen,
 *   never the panel power and never a sleep mode.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One-time setup. Call once from the UI thread after LVGL is ready and
 * before the first poll(); reads the initial configuration through the
 * Settings Service. Never fails the boot: on an internal error the
 * module stays inactive and input keeps working.
 */
esp_err_t xiaomiao_screen_idle_init(void);

/*
 * Re-read the configured idle minutes and run the state machine. Call
 * from the normal UI loop only (lvgl_task), never from the self test
 * builds (goal, "Scope and code entry").
 */
void xiaomiao_screen_idle_poll(void);

/*
 * Feed the debounced overall key state ("at least one of the six keys
 * is held") from the keypad indev read callback. Press and release
 * edges both refresh the activity time; the edge that ends an idle
 * period starts the wake swallow.
 */
void xiaomiao_screen_idle_report_key(bool any_pressed);

/*
 * True while the keypad read callback must report the key as released
 * and skip every downstream gesture handling: during the idle screen
 * and from the wake key press until all keys are released.
 */
bool xiaomiao_screen_idle_input_swallowed(void);

#ifdef __cplusplus
}
#endif
