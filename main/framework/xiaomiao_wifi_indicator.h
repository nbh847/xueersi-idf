/*
 * Framework Wi-Fi indicator (goal node 10, checkpoint 5).
 *
 * One global indicator, mounted on the LVGL top layer so it is visible
 * over the Launcher, every business App and the Hardware Test pages. It
 * is decoration only: it never takes the focus, never receives input and
 * is not part of the Navigation content root, so opening or closing an
 * App cannot disturb it.
 *
 * The indicator reads the Wi-Fi Service snapshot from an LVGL timer, so
 * every LVGL call stays on the UI thread; the Service's event callbacks
 * never touch LVGL. The Service itself samples the AP RSSI every five
 * seconds, which is what makes the levels move without scanning.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create the indicator. Call once, after LVGL and the display exist and
 * after the Wi-Fi Service was initialized. Returns ESP_ERR_INVALID_STATE
 * when it was already created or LVGL is not ready yet.
 */
esp_err_t xiaomiao_wifi_indicator_create(void);

/* Remove the indicator and its timer. Safe to call when it does not
 * exist. */
void xiaomiao_wifi_indicator_destroy(void);

#ifdef __cplusplus
}
#endif
