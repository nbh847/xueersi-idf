/*
 * Internal Wi-Fi Service interface (goal node 10).
 *
 * Shared between the Service implementation and the provisioning
 * module (SoftAP + DNS + HTTP page). Nothing outside main/services/ may
 * include this header: Apps and the Framework only see
 * xiaomiao_wifi_service.h.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "xiaomiao_wifi_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Result of the credential trial started from the provisioning page.
 *
 * The trial credentials live in RAM only. They reach the credential
 * store exclusively after the station received its IPv4 lease, so a
 * wrong password, a missing AP or a timeout leaves the previously saved
 * network untouched (goal node 10, "Credential persistence").
 */
typedef enum {
    XIAOMIAO_WIFI_ATTEMPT_IDLE = 0,
    XIAOMIAO_WIFI_ATTEMPT_RUNNING,
    XIAOMIAO_WIFI_ATTEMPT_SUCCEEDED,
    XIAOMIAO_WIFI_ATTEMPT_FAILED,
} xiaomiao_wifi_attempt_t;

/*
 * Trial-connect to `ssid`/`password`. Returns ESP_ERR_INVALID_STATE when
 * no provisioning session is active, and ESP_ERR_INVALID_ARG when the
 * SSID is empty or a field exceeds the supported length.
 */
esp_err_t xiaomiao_wifi_service_provision_connect(const char *ssid,
                                                 const char *password);

/* Current trial state; the provisioning page polls this. */
xiaomiao_wifi_attempt_t xiaomiao_wifi_service_attempt_state(void);

/* Raw error of the last failed trial; ESP_OK when there is none. */
esp_err_t xiaomiao_wifi_service_attempt_error(void);

/* Drop the trial result, so a retry starts from a clean state. */
void xiaomiao_wifi_service_attempt_reset(void);

/*
 * Provisioning session, implemented by xiaomiao_wifi_provisioning.c.
 * The Service owns the session decisions (when to start, when to close
 * after success or timeout); this module only moves the resources.
 */
esp_err_t xiaomiao_wifi_provisioning_session_start(void);
esp_err_t xiaomiao_wifi_provisioning_session_stop(void);
bool xiaomiao_wifi_provisioning_session_active(void);

/* Fill the live session details (SSID, temporary password, URL, seconds
 * left). Reports active=false and empty fields when there is no
 * session; the password exists for the device screen only. */
esp_err_t xiaomiao_wifi_provisioning_session_info(xiaomiao_wifi_provisioning_info_t *out_info);

#ifdef __cplusplus
}
#endif
