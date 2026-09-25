/*
 * Font Service self test (goal node 15B, CP3).
 *
 * Build-gated by XIAOMIAO_FONT_SERVICE_SELF_TEST, mutually exclusive
 * with the other self test options. Requires the flashed assets image
 * (font pack + invalid fixture). Success prints exactly one marker:
 *   FONT_SERVICE_SELF_TEST: PASS
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void xiaomiao_font_service_selftest_run(void);

#ifdef __cplusplus
}
#endif
