/*
 * Development self test for the Navigation layer (goal checkpoint 3).
 *
 * Compiled only when the CMake option XIAOMIAO_NAVIGATION_SELF_TEST is
 * ON. The boot path runs automatic assertions (error paths, open/back
 * cycles, object accounting) before the first screen refresh, then a
 * minimal root page drives the real A/B key path: three rounds of
 * "A opens the test App, B closes it, B verifies idempotent back".
 * Every assertion failure logs `NAVIGATION_SELF_TEST: FAIL` and aborts;
 * the single `NAVIGATION_SELF_TEST: PASS` line is the success marker
 * consumed by the flash/serial verification step.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the test App, run all automatic assertions, build the
 * minimal root page on the active screen and hook the key state
 * machine into `group`. Must be called once from the LVGL task before
 * the timer handler loop starts.
 */
void xiaomiao_navigation_selftest_run(lv_group_t *group);

#ifdef __cplusplus
}
#endif