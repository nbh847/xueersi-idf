/*
 * Minimal DNS responder for the provisioning hotspot (goal node 10).
 *
 * Answers every A query with the SoftAP address, which is what makes a
 * phone show its "sign in to network" sheet or send any typed address to
 * the configuration page. The deterministic entry point stays
 * http://192.168.4.1; a captive portal popup is only a convenience, so
 * nothing in the page depends on it (goal node 10, "Web page").
 *
 * Private to main/services/. The responder runs one task and one UDP
 * socket; stop() asks the task to leave and waits for it, so the port is
 * free before a later session reuses it.
 */

#pragma once

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start answering A queries with `answer`. Returns ESP_ERR_INVALID_STATE
 * when a responder is already running. */
esp_err_t xiaomiao_wifi_dns_start(esp_ip4_addr_t answer);

/* Stop the responder and release its socket. Idempotent. */
void xiaomiao_wifi_dns_stop(void);

#ifdef __cplusplus
}
#endif
