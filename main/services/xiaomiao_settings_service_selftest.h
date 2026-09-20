/*
 * Settings Service self test entry (goal node 9, checkpoint 2).
 *
 * Compiled only when the CMake option
 * XIAOMIAO_SETTINGS_SERVICE_SELF_TEST is ON.
 *
 * Each load path of the Service needs a different pre-existing NVS
 * state while xiaomiao_settings_service_init() is a one-shot call, so
 * the test is staged over boots: it keeps its own step marker in the
 * same namespace and advances one step per boot, restarting itself with
 * esp_restart() so the operator only has to flash once and watch the
 * log. The marker doubles as the witness that a repair rewrote only
 * `xiaomiao/settings` and left the other keys of the namespace alone.
 *
 * A failed check prints `SETTINGS_SERVICE_SELF_TEST: FAIL ...` and
 * halts the test task. The last step prints the single success marker
 * `SETTINGS_SERVICE_SELF_TEST: PASS` and restores the namespace to a
 * clean state, so the procedure is repeatable without erasing the
 * partition.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void xiaomiao_settings_service_selftest_run(void);

#ifdef __cplusplus
}
#endif
