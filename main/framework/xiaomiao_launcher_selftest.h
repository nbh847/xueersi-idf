/*
 * Development self test for the Launcher (goal node 3, checkpoint 4).
 *
 * Compiled only when the CMake option XIAOMIAO_LAUNCHER_SELF_TEST is ON.
 * The boot path runs automatic assertions first (empty registry,
 * duplicate create, movement and page boundaries, last-page fallback,
 * open before the Manager is ready, open/back cycles and LVGL object
 * accounting). It then switches to a guided key path: a status strip on
 * the top layer tells the human which key to press for each step and
 * every press is asserted against the expected focus and page.
 *
 * Any assertion failure logs `LAUNCHER_SELF_TEST: FAIL` and aborts. The
 * single `LAUNCHER_SELF_TEST: PASS` line is the success marker consumed
 * by the flash/serial verification step.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reset the Registry, register the test Apps, run the automatic
 * assertions, create the Launcher on the active screen and start the
 * guided key path on `group`. Must be called once from the LVGL task
 * before the timer handler loop starts.
 */
void xiaomiao_launcher_selftest_run(lv_group_t *group);

#ifdef __cplusplus
}
#endif
