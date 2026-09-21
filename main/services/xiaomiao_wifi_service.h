/*
 * Wi-Fi Service (design doc sections 7, 10, 12 and 15; goal node 10).
 *
 * Second System Service. It owns the whole Wi-Fi lifecycle: station
 * start, scan, connect, disconnect, back-off reconnect, the temporary
 * SoftAP provisioning session and the credential store. Apps and the
 * Framework only call this interface; no other firmware file calls
 * `esp_wifi_*`, `esp_netif_*`, the HTTP server or the credential NVS
 * key (goal node 10, "Architecture").
 *
 * The Service is asynchronous by design: init() brings the stack up and
 * returns without waiting for a scan, an association or a DHCP lease, so
 * a missing AP can never delay the Launcher (goal node 10, "Boot
 * integration"). Callers read progress through get_snapshot().
 *
 * Properties of the public snapshot:
 * - It carries no Wi-Fi password, no temporary SoftAP password, no HTTP
 *   form content and no internal pointer (goal node 10, "State model").
 * - get_snapshot() copies one consistent view: state and fields are read
 *   under the same lock, so a stale IP can never be paired with
 *   CONNECTED, and a disconnected snapshot always clears the old IP and
 *   RSSI.
 * - It stays readable after a failed init, with state ERROR.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMIAO_WIFI_SSID_MAX     32
/* One byte for the terminator on top of the largest SSID. */
#define XIAOMIAO_WIFI_SSID_BUF     33
#define XIAOMIAO_WIFI_PASSWORD_MAX 64
/* One byte for the terminator on top of the largest password. */
#define XIAOMIAO_WIFI_PASSWORD_BUF 65
/* The provisioning page lists at most ten visible networks. */
#define XIAOMIAO_WIFI_SCAN_MAX     10

/*
 * Lifecycle state. DISABLED means the user turned automatic connection
 * off: the Wi-Fi stack stays initialized, only the automatic paths stop
 * (goal node 10, "Following boots").
 */
typedef enum {
    XIAOMIAO_WIFI_UNINITIALIZED = 0,
    XIAOMIAO_WIFI_DISABLED,
    XIAOMIAO_WIFI_NO_CREDENTIALS,
    XIAOMIAO_WIFI_DISCONNECTED,
    XIAOMIAO_WIFI_SCANNING,
    XIAOMIAO_WIFI_CONNECTING,
    XIAOMIAO_WIFI_RETRY_WAIT,
    XIAOMIAO_WIFI_CONNECTED,
    XIAOMIAO_WIFI_PROVISIONING,
    XIAOMIAO_WIFI_AUTH_FAILED,
    XIAOMIAO_WIFI_ERROR,
} xiaomiao_wifi_state_t;

/*
 * One consistent view of the Service. `signal_level` is 0..4 and is 0
 * whenever the station is not connected. `last_disconnect_reason` is the
 * raw `wifi_err_reason_t` of the last disconnect, 0 when there is none.
 */
typedef struct {
    xiaomiao_wifi_state_t state;
    bool auto_connect;
    bool has_credentials;
    bool provisioning_active;
    char ssid[XIAOMIAO_WIFI_SSID_BUF];
    int8_t rssi;
    uint8_t signal_level;
    esp_ip4_addr_t ipv4;
    uint32_t retry_count;
    esp_err_t last_error;
    uint8_t last_disconnect_reason;
} xiaomiao_wifi_snapshot_t;

/* One scanned access point, as offered to the provisioning page. */
typedef struct {
    char ssid[XIAOMIAO_WIFI_SSID_BUF];
    int8_t rssi;
    bool secure;
} xiaomiao_wifi_ap_t;

/*
 * Address of the deterministic provisioning entry point. The temporary
 * SoftAP always uses the ESP-IDF default AP address.
 */
#define XIAOMIAO_WIFI_PROVISIONING_URL "http://192.168.4.1"
#define XIAOMIAO_WIFI_PROVISIONING_URL_BUF 24
/* Length of the random numeric SoftAP password. */
#define XIAOMIAO_WIFI_AP_PASSWORD_LEN 8
#define XIAOMIAO_WIFI_AP_PASSWORD_BUF 9

/*
 * Progress of the credential trial inside a session, published for the
 * device screen. Derived from the Service's internal attempt state so
 * Apps never need an internal header.
 */
typedef enum {
    XIAOMIAO_WIFI_SETUP_IDLE = 0,
    XIAOMIAO_WIFI_SETUP_CONNECTING,
    XIAOMIAO_WIFI_SETUP_CONNECTED,
    XIAOMIAO_WIFI_SETUP_FAILED,
    XIAOMIAO_WIFI_SETUP_SAVE_FAILED,
} xiaomiao_wifi_setup_state_t;

/*
 * Live information about the running provisioning session. `password` is
 * the temporary SoftAP password of this session; it exists only while
 * `active` is true and is shown on the device screen so the user can
 * join the hotspot. It is never logged and never placed in a snapshot
 * (goal node 10, "Data lifecycle and security boundary").
 */
typedef struct {
    bool active;
    char ssid[XIAOMIAO_WIFI_SSID_BUF];
    char password[XIAOMIAO_WIFI_AP_PASSWORD_BUF];
    char url[XIAOMIAO_WIFI_PROVISIONING_URL_BUF];
    uint32_t remaining_s;
    xiaomiao_wifi_setup_state_t setup_state;
    /* Raw error of the failed trial, so the screen can tell "wrong
     * password" from "network not found" without inventing a reason. */
    esp_err_t setup_error;
} xiaomiao_wifi_provisioning_info_t;

/*
 * Bring up the network stack, the STA interface and the Wi-Fi driver,
 * then consume the persisted `wifi_auto_connect` preference.
 *
 * Idempotent: only the first call touches the stack, later calls return
 * the first result. Never blocks on a scan, an association, DHCP or a
 * reconnect, and never aborts startup: a failure leaves the Service in
 * state ERROR with the raw error for get_snapshot() and lets the boot
 * continue to the Launcher (goal node 10, "Boot integration").
 */
esp_err_t xiaomiao_wifi_service_init(void);

/* Copy the current snapshot. ESP_ERR_INVALID_ARG on NULL,
 * ESP_ERR_INVALID_STATE before init(). */
esp_err_t xiaomiao_wifi_get_snapshot(xiaomiao_wifi_snapshot_t *out_snapshot);

/*
 * Apply a new `wifi_auto_connect` preference.
 *
 * Turning it off cancels a pending reconnect and disconnects the current
 * session, while the Wi-Fi stack stays initialized. Turning it on starts
 * a connection attempt when credentials exist. The Settings App persists
 * the preference through the Settings Service first and only then calls
 * this function (goal node 10, "Settings / Wi-Fi").
 */
esp_err_t xiaomiao_wifi_apply_auto_connect(bool enabled);

/* Start an asynchronous scan of the visible networks. The results are
 * read with xiaomiao_wifi_scan_get_results() once state leaves SCANNING. */
esp_err_t xiaomiao_wifi_scan_start(void);

/*
 * Copy the result of the last finished scan, strongest first, duplicates
 * removed, at most `capacity` entries. Sets *out_count to the number of
 * entries written and returns ESP_ERR_INVALID_STATE when no scan has
 * finished yet.
 */
esp_err_t xiaomiao_wifi_scan_get_results(xiaomiao_wifi_ap_t *results,
                                         size_t capacity,
                                         size_t *out_count);

/* Connect to the saved network. Used on boot and by the Settings App
 * after re-enabling automatic connection. */
esp_err_t xiaomiao_wifi_connect_saved(void);

/*
 * Disconnect the station. The saved credentials are kept, and the
 * automatic reconnect stays cancelled until the next explicit connect or
 * reboot (goal node 10, "Reconnect strategy").
 */
esp_err_t xiaomiao_wifi_disconnect(void);

/*
 * Delete the saved credentials only. The Settings Service data, every
 * other NVS key and the partition itself stay untouched; there is no
 * call to nvs_flash_erase() anywhere in this Service (goal node 10,
 * "Forget network").
 */
esp_err_t xiaomiao_wifi_forget_credentials(void);

/*
 * Start the temporary SoftAP provisioning session: a WPA2 hotspot named
 * `Xiaomiao-XXXX` with a freshly generated numeric password, a DNS
 * responder for captive-portal detection and the configuration page on
 * http://192.168.4.1. The session ends on success, on stop(), on the
 * ten-minute idle timeout or on a rollback after a failure.
 */
esp_err_t xiaomiao_wifi_provisioning_start(void);

/* Stop the session and release SoftAP, HTTP, DNS and scan resources. */
esp_err_t xiaomiao_wifi_provisioning_stop(void);

/* Read the live session details for the device screen. Inactive sessions
 * report active=false and empty fields. */
esp_err_t xiaomiao_wifi_provisioning_get_info(xiaomiao_wifi_provisioning_info_t *out_info);

#ifdef __cplusplus
}
#endif
