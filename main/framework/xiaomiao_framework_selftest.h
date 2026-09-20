/*
 * Development self test for the App Framework (goal checkpoint 4).
 *
 * Compiled only when the CMake option XIAOMIAO_FRAMEWORK_SELF_TEST is
 * ON. Any check failure logs `APP_FRAMEWORK_SELF_TEST: FAIL` and aborts;
 * the single `APP_FRAMEWORK_SELF_TEST: PASS` line is the success marker
 * consumed by the build/flash verification step.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void xiaomiao_framework_selftest_run(void);

#ifdef __cplusplus
}
#endif