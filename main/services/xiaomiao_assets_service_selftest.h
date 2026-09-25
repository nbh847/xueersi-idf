/*
 * Assets Service self test (goal node 15A, CP3).
 *
 * Build-gated by XIAOMIAO_ASSETS_SERVICE_SELF_TEST, mutually exclusive
 * with the other self test options. Runs on a normal `idf.py flash`
 * image: it expects the assets SPIFFS image (with manifest.txt) to be
 * present, and expects the Storage Service to be uninitialized so the
 * sd:/ namespace fails deterministically without a card.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void xiaomiao_assets_service_selftest_run(void);

#ifdef __cplusplus
}
#endif
