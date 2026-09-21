/*
 * Wi-Fi Service implementation (goal node 10).
 *
 * Responsibilities, in the order they appear below:
 *   1. credential store   - one versioned blob in the default NVS
 *   2. snapshot           - one locked, self-consistent view for the UI
 *   3. station lifecycle  - start, connect, disconnect, back-off retry
 *   4. scan               - asynchronous, results cached for the page
 *   5. provisioning       - session decisions, resource work is delegated
 *
 * Storage decision (goal node 10, "Credential persistence"): the Wi-Fi
 * driver runs with WIFI_STORAGE_RAM for its whole lifetime, and the
 * credentials live in the private `xiaomiao/wifi_creds` key. Two facts
 * from ESP-IDF v6.1 drive this:
 *   - WIFI_STORAGE_FLASH makes esp_wifi_set_config() write the station
 *     configuration into the `nvs.net80211` namespace immediately, so a
 *     trial connect from the page would overwrite the saved network
 *     before it ever reached an IPv4 lease.
 *   - esp_wifi_set_storage() is documented as a configuration call with
 *     no runtime switching guarantee, so "RAM while trying, FLASH after
 *     success" cannot be relied on.
 * The RAM-only driver plus an explicit commit into our own key keeps
 * both required properties: trial credentials stay in RAM, and the
 * saved network survives a wrong password, a missing AP or a timeout.
 */

#include "xiaomiao_wifi_service.h"
#include "xiaomiao_wifi_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "xiaomiao_settings_service.h"

static const char TAG[] = "wifi_svc";

/* Credential store identity (mirrors the Settings Service discipline:
 * one namespace every System Service shares, one key per Service). */
#define WIFI_NVS_NAMESPACE "xiaomiao"
#define WIFI_NVS_KEY       "wifi_creds"
#define WIFI_BLOB_VERSION  1
#define WIFI_BLOB_PAYLOAD  (XIAOMIAO_WIFI_SSID_MAX + XIAOMIAO_WIFI_PASSWORD_MAX)

/* Back-off schedule, in seconds; the last value repeats (goal node 10,
 * "Reconnect strategy"). */
static const uint32_t WIFI_RETRY_DELAY_S[] = { 1, 2, 5, 10, 30 };
#define WIFI_RETRY_DELAY_COUNT (sizeof(WIFI_RETRY_DELAY_S) / sizeof(WIFI_RETRY_DELAY_S[0]))

/* A connect attempt that never reaches an IPv4 lease is a failure, not a
 * permanent CONNECTING state (goal node 10, failure paths). */
#define WIFI_CONNECT_TIMEOUT_US (20 * 1000 * 1000)
/* How long a trial connect waits for the old association to go away
 * before issuing its own connect(). */
#define WIFI_TRIAL_LEAVE_WAIT_MS 1000
/* Signal sampling while connected. Reading the associated AP is cheap;
 * a periodic scan would disturb the link (goal node 10, checkpoint 5). */
#define WIFI_RSSI_REFRESH_US (5 * 1000 * 1000)
/* Provisioning teardown runs outside the event callback that triggered
 * it, so the HTTP response still reaches the phone. */
#define WIFI_FINISH_DELAY_US (50 * 1000)
/* Upper bound of the raw scan buffer; only the strongest
 * XIAOMIAO_WIFI_SCAN_MAX entries are kept afterwards. */
#define WIFI_SCAN_RAW_MAX 32

/*
 * Private on-Flash layout. Fixed width, never the memory image of a
 * public struct, so padding and future fields cannot drift into Flash.
 */
typedef struct {
    uint16_t schema_version;
    uint16_t payload_size;
    uint8_t ssid_len;
    uint8_t password_len;
    uint8_t reserved[2];
    uint8_t ssid[XIAOMIAO_WIFI_SSID_MAX];
    uint8_t password[XIAOMIAO_WIFI_PASSWORD_MAX];
} wifi_blob_v1_t;

_Static_assert(sizeof(wifi_blob_v1_t) == 104, "wifi credential blob v1 must stay 104 bytes");

/* In-memory credentials. The password buffer is wiped when the entry is
 * cleared (goal node 10, "Data lifecycle"). */
typedef struct {
    char ssid[XIAOMIAO_WIFI_SSID_BUF];
    char password[XIAOMIAO_WIFI_PASSWORD_BUF];
    uint8_t ssid_len;
    uint8_t password_len;
    bool valid;
} wifi_credentials_t;

static SemaphoreHandle_t s_lock;

static bool s_initialized;
static esp_err_t s_init_result = ESP_OK;

/* Protected by s_lock. */
static xiaomiao_wifi_snapshot_t s_snapshot;
static wifi_credentials_t s_saved;
static wifi_credentials_t s_trial;
static xiaomiao_wifi_attempt_t s_attempt;
static esp_err_t s_attempt_error = ESP_OK;
static xiaomiao_wifi_ap_t s_scan_results[XIAOMIAO_WIFI_SCAN_MAX];
static size_t s_scan_count;
static bool s_scan_finished;
static xiaomiao_wifi_state_t s_state_before_scan;

/* Only touched from the event loop, the timers and the caller's task;
 * every mutation happens under s_lock. */
static esp_netif_t *s_sta_netif;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_connect_timer;
static esp_timer_handle_t s_finish_timer;
static esp_timer_handle_t s_rssi_timer;
static uint32_t s_retry_step;
static bool s_station_started;
static bool s_user_disconnect;
/*
 * True while a trial connect drops the current link on purpose. The leave
 * it causes arrives as a normal disconnect event and must not be mistaken
 * for a failed trial.
 */
static bool s_trial_switching;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void wifi_lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void wifi_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void timer_stop(esp_timer_handle_t timer)
{
    if (timer != NULL) {
        /* A stopped or never started timer reports INVALID_STATE; that is
         * not an error here. */
        (void)esp_timer_stop(timer);
    }
}

static void timer_start_once(esp_timer_handle_t timer, uint64_t delay_us)
{
    if (timer == NULL) {
        return;
    }

    timer_stop(timer);
    const esp_err_t err = esp_timer_start_once(timer, delay_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timer restart failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    }
}

/* Four signal levels, as fixed by the goal's table; 0 means "no link". */
static uint8_t wifi_signal_level(int8_t rssi)
{
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -68) {
        return 3;
    }
    if (rssi >= -76) {
        return 2;
    }

    return 1;
}

static void credentials_clear(wifi_credentials_t *creds)
{
    if (creds == NULL) {
        return;
    }

    /* Wipe the secret before the length fields, so no copy of the
     * password survives in the buffer we hand back. */
    memset(creds->password, 0, sizeof(creds->password));
    memset(creds->ssid, 0, sizeof(creds->ssid));
    creds->ssid_len = 0;
    creds->password_len = 0;
    creds->valid = false;
}

static bool credentials_set(wifi_credentials_t *creds, const char *ssid, size_t ssid_len,
                            const char *password, size_t password_len)
{
    if (creds == NULL || ssid == NULL || ssid_len == 0 ||
        ssid_len > XIAOMIAO_WIFI_SSID_MAX || password_len > XIAOMIAO_WIFI_PASSWORD_MAX) {
        return false;
    }

    if (password_len > 0 && password == NULL) {
        return false;
    }

    credentials_clear(creds);
    memcpy(creds->ssid, ssid, ssid_len);
    if (password_len > 0) {
        memcpy(creds->password, password, password_len);
    }
    creds->ssid_len = (uint8_t)ssid_len;
    creds->password_len = (uint8_t)password_len;
    creds->valid = true;
    return true;
}

/*
 * Password policy: the ESP-IDF station accepts an open network (empty
 * password) or a WPA/WPA2/WPA3 passphrase of 8..63 characters, or the
 * 64-character hexadecimal PSK form. Anything else is rejected here as
 * well as on the page (goal node 10, "Data lifecycle").
 */
static bool wifi_password_is_supported(size_t length)
{
    if (length == 0 || length == 64) {
        return true;
    }

    return (length >= 8) && (length <= 63);
}

static const char *wifi_state_name(xiaomiao_wifi_state_t state)
{
    switch (state) {
    case XIAOMIAO_WIFI_DISABLED:
        return "disabled";
    case XIAOMIAO_WIFI_NO_CREDENTIALS:
        return "no_credentials";
    case XIAOMIAO_WIFI_DISCONNECTED:
        return "disconnected";
    case XIAOMIAO_WIFI_SCANNING:
        return "scanning";
    case XIAOMIAO_WIFI_CONNECTING:
        return "connecting";
    case XIAOMIAO_WIFI_RETRY_WAIT:
        return "retry_wait";
    case XIAOMIAO_WIFI_CONNECTED:
        return "connected";
    case XIAOMIAO_WIFI_PROVISIONING:
        return "provisioning";
    case XIAOMIAO_WIFI_AUTH_FAILED:
        return "auth_failed";
    case XIAOMIAO_WIFI_ERROR:
        return "error";
    case XIAOMIAO_WIFI_UNINITIALIZED:
    default:
        return "uninitialized";
    }
}

/* ------------------------------------------------------------------ */
/* Snapshot access (caller holds the lock)                              */
/* ------------------------------------------------------------------ */

static void snapshot_clear_link(void)
{
    s_snapshot.rssi = 0;
    s_snapshot.signal_level = 0;
    s_snapshot.ssid[0] = '\0';
    s_snapshot.ipv4.addr = 0;
}

static void snapshot_set_state(xiaomiao_wifi_state_t state)
{
    s_snapshot.state = state;
    if (state != XIAOMIAO_WIFI_CONNECTED) {
        /* A disconnected view never keeps the previous link's data
         * (goal node 10, "State model"). */
        snapshot_clear_link();
    }
}

static void snapshot_publish_state(xiaomiao_wifi_state_t state)
{
    wifi_lock();
    snapshot_set_state(state);
    s_snapshot.has_credentials = s_saved.valid;
    wifi_unlock();

    ESP_LOGI(TAG, "state=%s", wifi_state_name(state));
}

/* ------------------------------------------------------------------ */
/* Credential store                                                    */
/* ------------------------------------------------------------------ */

static void blob_encode(const wifi_credentials_t *creds, wifi_blob_v1_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema_version = WIFI_BLOB_VERSION;
    blob->payload_size = WIFI_BLOB_PAYLOAD;
    blob->ssid_len = creds->ssid_len;
    blob->password_len = creds->password_len;
    memcpy(blob->ssid, creds->ssid, creds->ssid_len);
    memcpy(blob->password, creds->password, creds->password_len);
}

static bool blob_is_valid(const wifi_blob_v1_t *blob, size_t length)
{
    if (length != sizeof(*blob)) {
        return false;
    }
    if (blob->schema_version != WIFI_BLOB_VERSION) {
        return false;
    }
    if (blob->payload_size != WIFI_BLOB_PAYLOAD) {
        return false;
    }
    if (blob->ssid_len == 0 || blob->ssid_len > XIAOMIAO_WIFI_SSID_MAX) {
        return false;
    }
    if (blob->password_len > XIAOMIAO_WIFI_PASSWORD_MAX) {
        return false;
    }

    return true;
}

/*
 * Load the saved network. A missing key is the first-boot case and not a
 * fault; a damaged blob is dropped in memory only, so a later commit
 * rewrites it. This function never erases the key or the partition.
 */
static esp_err_t credentials_load(wifi_credentials_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    credentials_clear(out);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    wifi_blob_v1_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t length = sizeof(blob);
    err = nvs_get_blob(handle, WIFI_NVS_KEY, &blob, &length);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }

    if (!blob_is_valid(&blob, length)) {
        ESP_LOGW(TAG, "stored credentials invalid (length=%u), ignoring", (unsigned)length);
        return ESP_ERR_INVALID_SIZE;
    }

    blob.ssid[sizeof(blob.ssid) - 1] = '\0';
    blob.password[sizeof(blob.password) - 1] = '\0';
    return credentials_set(out, (const char *)blob.ssid, blob.ssid_len,
                           (const char *)blob.password, blob.password_len)
               ? ESP_OK
               : ESP_ERR_INVALID_SIZE;
}

/* Commit one credential set. Touches exactly one key of one namespace. */
static esp_err_t credentials_store(const wifi_credentials_t *creds)
{
    if (creds == NULL || !creds->valid) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    wifi_blob_v1_t blob;
    blob_encode(creds, &blob);
    err = nvs_set_blob(handle, WIFI_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return err;
}

/* Drop the saved network. Only this key is removed (goal node 10,
 * "Forget network"): no nvs_flash_erase(), no partition change. */
static esp_err_t credentials_erase(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, WIFI_NVS_KEY);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    else if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Nothing stored: forgetting is already satisfied. */
        err = ESP_OK;
    }
    nvs_close(handle);

    return err;
}

/* ------------------------------------------------------------------ */
/* Station control                                                     */
/* ------------------------------------------------------------------ */

/*
 * Push one credential set into the driver. WIFI_STORAGE_RAM is in force,
 * so this only touches RAM (see the storage note at the top).
 */
static esp_err_t station_apply_config(const wifi_credentials_t *creds)
{
    wifi_config_t config;
    memset(&config, 0, sizeof(config));

    if (creds != NULL && creds->valid) {
        memcpy(config.sta.ssid, creds->ssid, creds->ssid_len);
        memcpy(config.sta.password, creds->password, creds->password_len);
        /* An empty password means an open network; asking for WPA2 would
         * make the station reject it during the scan. */
        config.sta.threshold.authmode = (creds->password_len > 0) ? WIFI_AUTH_WPA2_PSK
                                                                  : WIFI_AUTH_OPEN;
    }
    else {
        /* No credentials: clear the station configuration so a leftover
         * trial network cannot be reused. */
        config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

/* Copy the credentials the caller must not keep a pointer to. */
static void credentials_copy(wifi_credentials_t *out, const wifi_credentials_t *in)
{
    *out = *in;
}

static uint32_t retry_delay_s(uint32_t step)
{
    if (step >= WIFI_RETRY_DELAY_COUNT) {
        return WIFI_RETRY_DELAY_S[WIFI_RETRY_DELAY_COUNT - 1];
    }

    return WIFI_RETRY_DELAY_S[step];
}

static bool disconnect_reason_is_auth(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return true;
    default:
        return false;
    }
}

static void station_cancel_timers(void)
{
    timer_stop(s_retry_timer);
    timer_stop(s_connect_timer);
}

/* Ask the driver for a connection with whatever configuration is in
 * RAM. Never called with the lock held, and never starts a second
 * attempt while one is in flight or while a link is already up, so the
 * reported state always matches the driver. */
static void station_connect_now(void)
{
    wifi_lock();
    const bool allowed = s_snapshot.auto_connect && s_saved.valid && !s_user_disconnect;
    const bool already_running = (s_snapshot.state == XIAOMIAO_WIFI_CONNECTING) ||
                                 (s_snapshot.state == XIAOMIAO_WIFI_CONNECTED);
    if (allowed && !already_running) {
        snapshot_set_state(XIAOMIAO_WIFI_CONNECTING);
        s_snapshot.last_error = ESP_OK;
    }
    wifi_unlock();

    if (!allowed || already_running) {
        return;
    }

    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
        return;
    }

    timer_start_once(s_connect_timer, WIFI_CONNECT_TIMEOUT_US);
}

static void station_schedule_retry(void)
{
    uint32_t step = 0;
    uint32_t delay_s = 0;

    wifi_lock();
    step = s_retry_step;
    delay_s = retry_delay_s(step);
    if (s_retry_step + 1 < WIFI_RETRY_DELAY_COUNT) {
        s_retry_step++;
    }
    s_snapshot.retry_count = (uint32_t)s_retry_step + 1;
    snapshot_set_state(XIAOMIAO_WIFI_RETRY_WAIT);
    wifi_unlock();

    ESP_LOGI(TAG, "reconnect in %u s (attempt %u)", (unsigned)delay_s, (unsigned)(step + 1));
    timer_start_once(s_retry_timer, (uint64_t)delay_s * 1000000ULL);
}

static void station_reset_retry(void)
{
    wifi_lock();
    s_retry_step = 0;
    s_snapshot.retry_count = 0;
    wifi_unlock();
    timer_stop(s_retry_timer);
}

/*
 * Put the saved network back into the driver and join it.
 *
 * Needed after a provisioning session ends: leaving APSTA restarts the
 * Wi-Fi driver, which drops the association and can leave the RAM
 * configuration unset. The restart also queues a disconnect event, so
 * the state may briefly pass through RETRY_WAIT; the retry timer and
 * this call converge on the same network either way.
 */
static void station_reconnect_saved(void)
{
    wifi_lock();
    const bool allowed = s_saved.valid && s_snapshot.auto_connect;
    wifi_unlock();

    if (!allowed) {
        return;
    }

    const esp_err_t err = station_apply_config(&s_saved);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "restoring the saved network failed: %s (0x%x)", esp_err_to_name(err),
                 (unsigned)err);
        return;
    }

    s_user_disconnect = false;
    station_reset_retry();
    station_connect_now();
}

static void station_mark_connected(void)
{
    wifi_ap_record_t info;
    memset(&info, 0, sizeof(info));

    /* The AP record gives the real SSID and RSSI of the link we are on.
     * A failure here must not fake values: the snapshot then keeps the
     * state without inventing an SSID. */
    const esp_err_t info_err = esp_wifi_sta_get_ap_info(&info);

    /*
     * The address is read back from the interface rather than carried
     * over from a GOT_IP event, so this also works when the association
     * outlived a provisioning session and no new event will arrive.
     */
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    const bool ip_ok = (s_sta_netif != NULL) &&
                       (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) &&
                       (ip_info.ip.addr != 0);

    wifi_lock();
    if (info_err == ESP_OK) {
        const size_t ssid_len = strnlen((const char *)info.ssid, sizeof(info.ssid));
        const size_t copy_len = (ssid_len < XIAOMIAO_WIFI_SSID_MAX) ? ssid_len : XIAOMIAO_WIFI_SSID_MAX;
        memcpy(s_snapshot.ssid, info.ssid, copy_len);
        s_snapshot.ssid[copy_len] = '\0';
        s_snapshot.rssi = info.rssi;
        s_snapshot.signal_level = wifi_signal_level(info.rssi);
    }
    if (ip_ok) {
        s_snapshot.ipv4 = ip_info.ip;
    }
    s_snapshot.state = XIAOMIAO_WIFI_CONNECTED;
    s_snapshot.last_error = ESP_OK;
    s_snapshot.last_disconnect_reason = 0;
    s_retry_step = 0;
    s_snapshot.retry_count = 0;
    wifi_unlock();

    timer_stop(s_retry_timer);
    timer_stop(s_connect_timer);
}

/*
 * Settle the link state after a provisioning session ended.
 *
 * Leaving APSTA closes the SoftAP but does not touch an established
 * station association, so the driver is asked what the link really is
 * instead of assuming the session tore it down. Guessing here used to
 * disconnect a healthy link and pay a full connect timeout before the
 * retry timer brought it back (goal node 10, follow-up).
 */
static void station_sync_after_session(void)
{
    wifi_ap_record_t info;
    memset(&info, 0, sizeof(info));

    if (s_saved.valid && esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        station_mark_connected();
        return;
    }

    wifi_lock();
    const bool auto_connect = s_snapshot.auto_connect;
    const bool has_credentials = s_saved.valid;
    wifi_unlock();

    if (!auto_connect) {
        snapshot_publish_state(XIAOMIAO_WIFI_DISABLED);
        return;
    }
    if (!has_credentials) {
        snapshot_publish_state(XIAOMIAO_WIFI_NO_CREDENTIALS);
        return;
    }

    station_reconnect_saved();
}

/* ------------------------------------------------------------------ */
/* Event handling                                                      */
/* ------------------------------------------------------------------ */

static void handle_scan_done(void)
{
    uint16_t count = 0;
    if (esp_wifi_scan_get_ap_num(&count) != ESP_OK) {
        count = 0;
    }

    static wifi_ap_record_t records[WIFI_SCAN_RAW_MAX];
    uint16_t fetch = (count > WIFI_SCAN_RAW_MAX) ? WIFI_SCAN_RAW_MAX : count;
    memset(records, 0, sizeof(records));

    if (fetch > 0 && esp_wifi_scan_get_ap_records(&fetch, records) != ESP_OK) {
        fetch = 0;
    }

    xiaomiao_wifi_ap_t kept[XIAOMIAO_WIFI_SCAN_MAX];
    memset(kept, 0, sizeof(kept));
    size_t kept_count = 0;

    for (uint16_t i = 0; i < fetch; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        const size_t ssid_len = strnlen(ssid, sizeof(records[i].ssid));
        if (ssid_len == 0) {
            /* Hidden networks are out of scope for this node. */
            continue;
        }

        char name[XIAOMIAO_WIFI_SSID_BUF];
        const size_t copy_len = (ssid_len < XIAOMIAO_WIFI_SSID_MAX) ? ssid_len : XIAOMIAO_WIFI_SSID_MAX;
        memcpy(name, ssid, copy_len);
        name[copy_len] = '\0';

        /* Duplicate SSIDs collapse to the strongest BSSID, so the page
         * shows one entry per network (goal node 10, "Web page"). */
        size_t existing = kept_count;
        for (size_t k = 0; k < kept_count; ++k) {
            if (strcmp(kept[k].ssid, name) == 0) {
                existing = k;
                break;
            }
        }

        if (existing < kept_count) {
            if (records[i].rssi > kept[existing].rssi) {
                kept[existing].rssi = records[i].rssi;
                kept[existing].secure = (records[i].authmode != WIFI_AUTH_OPEN);
            }
            continue;
        }

        if (kept_count < XIAOMIAO_WIFI_SCAN_MAX) {
            memcpy(kept[kept_count].ssid, name, copy_len + 1);
            kept[kept_count].rssi = records[i].rssi;
            kept[kept_count].secure = (records[i].authmode != WIFI_AUTH_OPEN);
            kept_count++;
        }
    }

    /* Strongest first. */
    for (size_t i = 1; i < kept_count; ++i) {
        xiaomiao_wifi_ap_t entry = kept[i];
        size_t j = i;
        while (j > 0 && kept[j - 1].rssi < entry.rssi) {
            kept[j] = kept[j - 1];
            j--;
        }
        kept[j] = entry;
    }

    wifi_lock();
    memcpy(s_scan_results, kept, sizeof(kept));
    s_scan_count = kept_count;
    s_scan_finished = true;
    if (s_snapshot.state == XIAOMIAO_WIFI_SCANNING) {
        s_snapshot.state = s_state_before_scan;
    }
    wifi_unlock();

    ESP_LOGI(TAG, "scan done, %u network(s) listed", (unsigned)kept_count);
}

static void handle_station_disconnected(const wifi_event_sta_disconnected_t *event)
{
    const uint8_t reason = (event != NULL) ? event->reason : 0;
    xiaomiao_wifi_attempt_t attempt;
    bool user_disconnect;
    bool auto_connect;
    bool has_credentials;
    bool switching;

    wifi_lock();
    attempt = s_attempt;
    user_disconnect = s_user_disconnect;
    auto_connect = s_snapshot.auto_connect;
    has_credentials = s_saved.valid;
    switching = s_trial_switching;
    s_snapshot.last_disconnect_reason = reason;
    s_snapshot.last_error = ESP_OK;
    wifi_unlock();

    timer_stop(s_connect_timer);

    if (attempt == XIAOMIAO_WIFI_ATTEMPT_RUNNING &&
        (switching || reason == WIFI_REASON_ASSOC_LEAVE)) {
        /*
         * Expected: the trial connect dropped the current link before
         * trying the new network, either while it was waiting for the
         * leave (switching) or a moment later, when its own disconnect
         * event arrives with ASSOC_LEAVE. Neither means the trial failed.
         */
        return;
    }

    if (attempt == XIAOMIAO_WIFI_ATTEMPT_RUNNING) {
        /*
         * A trial connect from the page. The saved network is still in
         * Flash and untouched: report the failure, put the saved station
         * configuration back and let the user submit again (goal node 10,
         * failure paths).
         */
        const esp_err_t mapped = disconnect_reason_is_auth(reason) ? ESP_ERR_INVALID_STATE
                                                                  : ESP_ERR_NOT_FOUND;

        wifi_lock();
        s_attempt = XIAOMIAO_WIFI_ATTEMPT_FAILED;
        s_attempt_error = mapped;
        snapshot_set_state(disconnect_reason_is_auth(reason) ? XIAOMIAO_WIFI_AUTH_FAILED
                                                            : XIAOMIAO_WIFI_DISCONNECTED);
        s_snapshot.has_credentials = s_saved.valid;
        wifi_unlock();

        (void)station_apply_config(&s_saved);
        ESP_LOGW(TAG, "trial connect failed, reason=%u, saved network kept", (unsigned)reason);
        return;
    }

    if (xiaomiao_wifi_provisioning_session_active()) {
        /*
         * Inside a provisioning session a station drop is expected:
         * switching to APSTA restarts the driver. Reconnecting the old
         * network now would only fight with the network the user is
         * picking, and the Service puts the saved network back when the
         * session ends.
         */
        snapshot_publish_state(XIAOMIAO_WIFI_PROVISIONING);
        return;
    }

    if (user_disconnect) {
        snapshot_publish_state(has_credentials ? XIAOMIAO_WIFI_DISCONNECTED
                                               : XIAOMIAO_WIFI_NO_CREDENTIALS);
        return;
    }

    if (!auto_connect) {
        snapshot_publish_state(XIAOMIAO_WIFI_DISABLED);
        return;
    }

    if (disconnect_reason_is_auth(reason)) {
        /* Wrong password on the saved network. High-frequency retries
         * cannot fix it, so wait for a new configuration (goal node 10,
         * "Reconnect strategy"). */
        wifi_lock();
        s_snapshot.last_error = ESP_ERR_INVALID_STATE;
        wifi_unlock();
        snapshot_publish_state(XIAOMIAO_WIFI_AUTH_FAILED);
        return;
    }

    if (!has_credentials) {
        snapshot_publish_state(XIAOMIAO_WIFI_NO_CREDENTIALS);
        return;
    }

    station_schedule_retry();
}

static void handle_got_ip(const ip_event_got_ip_t *event)
{
    xiaomiao_wifi_attempt_t attempt;
    wifi_credentials_t trial = { 0 };
    bool has_trial = false;

    wifi_lock();
    attempt = s_attempt;
    if (attempt == XIAOMIAO_WIFI_ATTEMPT_RUNNING) {
        credentials_copy(&trial, &s_trial);
        has_trial = trial.valid;
    }
    if (event != NULL) {
        s_snapshot.ipv4 = event->ip_info.ip;
    }
    wifi_unlock();

    if (attempt == XIAOMIAO_WIFI_ATTEMPT_RUNNING && has_trial) {
        /*
         * The trial network answered with an IPv4 lease, so it is now the
         * network to keep. Only here does the credential store change
         * (goal node 10, "Credential persistence").
         */
        const esp_err_t store_err = credentials_store(&trial);

        wifi_lock();
        if (store_err == ESP_OK) {
            /* Publish the replacement only after NVS committed it. A
             * failed write must leave both the persisted and in-memory
             * saved network untouched. */
            credentials_copy(&s_saved, &trial);
        }
        s_attempt = (store_err == ESP_OK) ? XIAOMIAO_WIFI_ATTEMPT_SUCCEEDED
                                          : XIAOMIAO_WIFI_ATTEMPT_FAILED;
        s_attempt_error = store_err;
        s_snapshot.has_credentials = s_saved.valid;
        wifi_unlock();

        if (store_err != ESP_OK) {
            /* Connected but not persisted: never report the session as
             * finished (goal node 10, failure paths). */
            ESP_LOGE(TAG, "saving credentials failed: %s (0x%x), connected but not saved",
                     esp_err_to_name(store_err), (unsigned)store_err);
        }
        else {
            ESP_LOGI(TAG, "credentials saved, provisioning can finish");
        }

        station_mark_connected();
        if (store_err == ESP_OK) {
            /* Close the session from a timer: the event callback must
             * stay short and the HTTP response still has to reach the
             * phone. A save failure keeps the page open so it can show
             * the error and let the user retry. */
            timer_start_once(s_finish_timer, WIFI_FINISH_DELAY_US);
        }
        return;
    }

    station_mark_connected();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch (id) {
    case WIFI_EVENT_STA_START:
        s_station_started = true;
        /* The first automatic attempt happens here and never blocks the
         * boot chain (goal node 10, "Boot connect"). */
        station_connect_now();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        handle_station_disconnected((const wifi_event_sta_disconnected_t *)data);
        break;
    case WIFI_EVENT_SCAN_DONE:
        handle_scan_done();
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id == IP_EVENT_STA_GOT_IP) {
        handle_got_ip((const ip_event_got_ip_t *)data);
    }
}

/* ------------------------------------------------------------------ */
/* Timers                                                              */
/* ------------------------------------------------------------------ */

static void retry_timer_cb(void *arg)
{
    (void)arg;

    /* Called from the esp_timer task, never from an ISR. */
    station_connect_now();
}

static void connect_timeout_cb(void *arg)
{
    (void)arg;

    bool still_connecting;
    wifi_lock();
    still_connecting = (s_snapshot.state == XIAOMIAO_WIFI_CONNECTING);
    if (still_connecting) {
        s_snapshot.last_error = ESP_ERR_TIMEOUT;
    }
    wifi_unlock();

    if (!still_connecting) {
        return;
    }

    ESP_LOGW(TAG, "connect attempt timed out before an IPv4 lease");

    /*
     * The disconnect event normally drives the next step (trial failure
     * or back-off). When the station never associated there is no event
     * to wait for, so the same handling runs directly; either way this
     * path cannot leave CONNECTING behind.
     */
    const esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        const wifi_event_sta_disconnected_t event = { .reason = WIFI_REASON_CONNECTION_FAIL };
        handle_station_disconnected(&event);
    }
    else if (err != ESP_OK) {
        ESP_LOGW(TAG, "disconnect after timeout failed: %s (0x%x)", esp_err_to_name(err),
                 (unsigned)err);
        station_schedule_retry();
    }
}

static void finish_timer_cb(void *arg)
{
    (void)arg;

    /* Success path: release the provisioning resources and return to
     * plain station mode. */
    const esp_err_t err = xiaomiao_wifi_provisioning_session_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "releasing the provisioning session failed: %s (0x%x)",
                 esp_err_to_name(err), (unsigned)err);
    }

    wifi_lock();
    s_snapshot.provisioning_active = false;
    const xiaomiao_wifi_attempt_t attempt = s_attempt;
    s_snapshot.has_credentials = s_saved.valid;
    wifi_unlock();

    /*
     * Keep the link the trial just established, or bring the saved
     * network back if the association did not survive the mode switch.
     */
    station_sync_after_session();

    ESP_LOGI(TAG, "provisioning session closed after success (attempt=%d)", (int)attempt);
}

/*
 * Keep the reported signal strength honest while connected. The
 * associated AP can be asked directly, so no scan is needed and the
 * levels move without disturbing traffic.
 */
static void rssi_timer_cb(void *arg)
{
    (void)arg;

    wifi_lock();
    const bool connected = (s_snapshot.state == XIAOMIAO_WIFI_CONNECTED);
    wifi_unlock();

    if (!connected) {
        return;
    }

    wifi_ap_record_t info;
    memset(&info, 0, sizeof(info));
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) {
        return;
    }

    wifi_lock();
    s_snapshot.rssi = info.rssi;
    s_snapshot.signal_level = wifi_signal_level(info.rssi);
    wifi_unlock();
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t xiaomiao_wifi_service_init(void)
{
    if (s_initialized) {
        return s_init_result;
    }
    s_initialized = true;

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = XIAOMIAO_WIFI_UNINITIALIZED;
    s_snapshot.last_error = ESP_OK;
    s_attempt = XIAOMIAO_WIFI_ATTEMPT_IDLE;

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        s_init_result = ESP_ERR_NO_MEM;
        s_snapshot.state = XIAOMIAO_WIFI_ERROR;
        ESP_LOGE(TAG, "mutex allocation failed");
        return s_init_result;
    }

    /*
     * Consume the persisted preference. When the Settings Service is not
     * readable the Service stays off the air instead of guessing: an
     * unavailable configuration is no reason to join a network on its
     * own (goal node 10, "Following boots").
     */
    xiaomiao_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    bool auto_connect = false;
    const esp_err_t settings_err = xiaomiao_settings_get(&settings);
    if (settings_err == ESP_OK) {
        auto_connect = settings.wifi_auto_connect;
    }
    else {
        ESP_LOGW(TAG, "settings unavailable: %s (0x%x), automatic connect stays off",
                 esp_err_to_name(settings_err), (unsigned)settings_err);
    }

    /* A missing or damaged entry is not fatal: the station simply has no
     * network to join yet. */
    const esp_err_t creds_err = credentials_load(&s_saved);
    if (creds_err == ESP_OK) {
        ESP_LOGI(TAG, "saved network loaded, ssid='%s'", s_saved.ssid);
    }
    else if (creds_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no saved network yet");
    }
    else {
        ESP_LOGW(TAG, "credential load failed: %s (0x%x)", esp_err_to_name(creds_err),
                 (unsigned)creds_err);
        s_snapshot.last_error = creds_err;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        goto fail;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        goto fail;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        err = ESP_FAIL;
        goto fail;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        goto fail;
    }

    /*
     * RAM-only driver state: the station configuration must never reach
     * Flash before the page's trial connect succeeded (see the storage
     * note at the top of this file).
     */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        goto fail;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (err != ESP_OK) {
        goto fail;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
    if (err != ESP_OK) {
        goto fail;
    }

    const esp_timer_create_args_t retry_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
    };
    err = esp_timer_create(&retry_args, &s_retry_timer);
    if (err != ESP_OK) {
        goto fail;
    }

    const esp_timer_create_args_t connect_args = {
        .callback = connect_timeout_cb,
        .name = "wifi_conn_to",
    };
    err = esp_timer_create(&connect_args, &s_connect_timer);
    if (err != ESP_OK) {
        goto fail;
    }

    const esp_timer_create_args_t finish_args = {
        .callback = finish_timer_cb,
        .name = "wifi_finish",
    };
    err = esp_timer_create(&finish_args, &s_finish_timer);
    if (err != ESP_OK) {
        goto fail;
    }

    const esp_timer_create_args_t rssi_args = {
        .callback = rssi_timer_cb,
        .name = "wifi_rssi",
    };
    err = esp_timer_create(&rssi_args, &s_rssi_timer);
    if (err != ESP_OK) {
        goto fail;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        goto fail;
    }

    /*
     * RAM storage means the driver comes up without a station
     * configuration, so the saved network is pushed in before start();
     * the STA_START handler then has something real to connect to.
     */
    err = station_apply_config(&s_saved);
    if (err != ESP_OK) {
        goto fail;
    }

    wifi_lock();
    s_snapshot.auto_connect = auto_connect;
    s_snapshot.has_credentials = s_saved.valid;
    if (!auto_connect) {
        s_snapshot.state = XIAOMIAO_WIFI_DISABLED;
    }
    else if (!s_saved.valid) {
        s_snapshot.state = XIAOMIAO_WIFI_NO_CREDENTIALS;
    }
    else {
        s_snapshot.state = XIAOMIAO_WIFI_DISCONNECTED;
    }
    wifi_unlock();

    err = esp_wifi_start();
    if (err != ESP_OK) {
        goto fail;
    }

    err = esp_timer_start_periodic(s_rssi_timer, WIFI_RSSI_REFRESH_US);
    if (err != ESP_OK) {
        goto fail;
    }

    /* The station start event triggers the first attempt; this call
     * returns immediately either way (goal node 10, "Boot integration"). */
    ESP_LOGI(TAG, "service ready, auto_connect=%d, has_credentials=%d",
             (int)auto_connect, (int)s_saved.valid);
    s_init_result = ESP_OK;
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "wifi service init failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    wifi_lock();
    s_snapshot.state = XIAOMIAO_WIFI_ERROR;
    s_snapshot.last_error = err;
    s_snapshot.has_credentials = s_saved.valid;
    wifi_unlock();
    s_init_result = err;
    return err;
}

esp_err_t xiaomiao_wifi_get_snapshot(xiaomiao_wifi_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_lock();
    *out_snapshot = s_snapshot;
    wifi_unlock();

    /* Read the session flag after releasing the Service lock: the two
     * locks are never held at the same time, so no lock order can
     * deadlock. */
    out_snapshot->provisioning_active = xiaomiao_wifi_provisioning_session_active();

    return ESP_OK;
}

esp_err_t xiaomiao_wifi_apply_auto_connect(bool enabled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_lock();
    s_snapshot.auto_connect = enabled;
    const bool had_credentials = s_saved.valid;
    wifi_unlock();

    if (!enabled) {
        /* Keep the stack, drop the automatic paths (goal node 10,
         * "Settings / Wi-Fi"). */
        s_user_disconnect = true;
        station_cancel_timers();
        (void)esp_wifi_disconnect();
        snapshot_publish_state(XIAOMIAO_WIFI_DISABLED);
        ESP_LOGI(TAG, "automatic connect disabled");
        return ESP_OK;
    }

    s_user_disconnect = false;
    if (!had_credentials) {
        snapshot_publish_state(XIAOMIAO_WIFI_NO_CREDENTIALS);
        return ESP_OK;
    }

    station_connect_now();
    return ESP_OK;
}

esp_err_t xiaomiao_wifi_scan_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_station_started) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_lock();
    if (s_snapshot.state == XIAOMIAO_WIFI_SCANNING) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_state_before_scan = s_snapshot.state;
    s_scan_finished = false;
    s_snapshot.state = XIAOMIAO_WIFI_SCANNING;
    wifi_unlock();

    const esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        wifi_lock();
        s_snapshot.state = s_state_before_scan;
        wifi_unlock();
        ESP_LOGW(TAG, "scan start failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    }

    return err;
}

esp_err_t xiaomiao_wifi_scan_get_results(xiaomiao_wifi_ap_t *results, size_t capacity,
                                         size_t *out_count)
{
    if (results == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_lock();
    if (!s_scan_finished) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const size_t count = (s_scan_count < capacity) ? s_scan_count : capacity;
    memcpy(results, s_scan_results, count * sizeof(results[0]));
    wifi_unlock();

    *out_count = count;
    return ESP_OK;
}

esp_err_t xiaomiao_wifi_connect_saved(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_lock();
    const bool has_credentials = s_saved.valid;
    const bool auto_connect = s_snapshot.auto_connect;
    wifi_unlock();

    if (!has_credentials) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!auto_connect) {
        return ESP_ERR_INVALID_STATE;
    }

    s_user_disconnect = false;
    station_reset_retry();

    const esp_err_t err = station_apply_config(&s_saved);
    if (err != ESP_OK) {
        return err;
    }

    station_connect_now();
    return ESP_OK;
}

esp_err_t xiaomiao_wifi_disconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_user_disconnect = true;
    station_cancel_timers();

    const esp_err_t err = esp_wifi_disconnect();
    /* The station was already idle: report success, nothing to undo. */
    if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        snapshot_publish_state(s_saved.valid ? XIAOMIAO_WIFI_DISCONNECTED
                                            : XIAOMIAO_WIFI_NO_CREDENTIALS);
        return ESP_OK;
    }

    return err;
}

esp_err_t xiaomiao_wifi_forget_credentials(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop every automatic path first, so a reconnect cannot race the
     * deletion. */
    s_user_disconnect = true;
    station_cancel_timers();

    const esp_err_t erase_err = credentials_erase();

    wifi_lock();
    if (erase_err != ESP_OK) {
        /* Keep reporting what is really stored (goal node 10, failure
         * paths): no pretending the network was forgotten. */
        s_snapshot.last_error = erase_err;
        wifi_unlock();
        ESP_LOGE(TAG, "forget network failed: %s (0x%x)", esp_err_to_name(erase_err),
                 (unsigned)erase_err);
        return erase_err;
    }

    credentials_clear(&s_saved);
    credentials_clear(&s_trial);
    s_attempt = XIAOMIAO_WIFI_ATTEMPT_IDLE;
    s_attempt_error = ESP_OK;
    s_snapshot.has_credentials = false;
    wifi_unlock();

    const esp_err_t apply_err = station_apply_config(NULL);
    if (apply_err != ESP_OK) {
        ESP_LOGW(TAG, "clearing the station configuration failed: %s (0x%x)",
                 esp_err_to_name(apply_err), (unsigned)apply_err);
    }

    (void)esp_wifi_disconnect();
    snapshot_publish_state(XIAOMIAO_WIFI_NO_CREDENTIALS);
    ESP_LOGI(TAG, "saved network forgotten");
    return ESP_OK;
}

esp_err_t xiaomiao_wifi_provisioning_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xiaomiao_wifi_provisioning_session_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* A fresh session starts from a clean trial state. */
    wifi_lock();
    s_attempt = XIAOMIAO_WIFI_ATTEMPT_IDLE;
    s_attempt_error = ESP_OK;
    wifi_unlock();

    const esp_err_t err = xiaomiao_wifi_provisioning_session_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "provisioning start failed: %s (0x%x)", esp_err_to_name(err),
                 (unsigned)err);
        wifi_lock();
        s_snapshot.last_error = err;
        wifi_unlock();
        return err;
    }

    wifi_lock();
    s_snapshot.provisioning_active = true;
    s_snapshot.last_error = ESP_OK;
    snapshot_set_state(XIAOMIAO_WIFI_PROVISIONING);
    wifi_unlock();

    ESP_LOGI(TAG, "provisioning session started");
    return ESP_OK;
}

esp_err_t xiaomiao_wifi_provisioning_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!xiaomiao_wifi_provisioning_session_active()) {
        /* Idempotent: stopping an idle session is not an error. */
        return ESP_OK;
    }

    const esp_err_t err = xiaomiao_wifi_provisioning_session_stop();

    wifi_lock();
    s_snapshot.provisioning_active = false;
    if (s_attempt == XIAOMIAO_WIFI_ATTEMPT_RUNNING) {
        /* The user cancelled while a trial was in flight. */
        s_attempt = XIAOMIAO_WIFI_ATTEMPT_IDLE;
    }
    s_trial_switching = false;
    s_snapshot.has_credentials = s_saved.valid;
    wifi_unlock();

    /*
     * Whether a link survived is the driver's answer, not this function's
     * guess: closing the SoftAP does not drop an established station
     * association. Disconnecting on purpose here cost a full connect
     * timeout before the retry brought the network back.
     */
    station_sync_after_session();

    ESP_LOGI(TAG, "provisioning session stopped");
    return err;
}

esp_err_t xiaomiao_wifi_provisioning_get_info(xiaomiao_wifi_provisioning_info_t *out_info)
{
    if (out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));

    const esp_err_t err = xiaomiao_wifi_provisioning_session_info(out_info);
    if (err != ESP_OK || !out_info->active) {
        return err;
    }

    /* The trial progress lives in the Service; publish it as a plain
     * state so the UI never needs the internal header. */
    wifi_lock();
    const xiaomiao_wifi_attempt_t attempt = s_attempt;
    const esp_err_t detail = s_attempt_error;
    const bool connected = (s_snapshot.state == XIAOMIAO_WIFI_CONNECTED);
    wifi_unlock();

    switch (attempt) {
    case XIAOMIAO_WIFI_ATTEMPT_RUNNING:
        out_info->setup_state = XIAOMIAO_WIFI_SETUP_CONNECTING;
        break;
    case XIAOMIAO_WIFI_ATTEMPT_SUCCEEDED:
        out_info->setup_state = XIAOMIAO_WIFI_SETUP_CONNECTED;
        break;
    case XIAOMIAO_WIFI_ATTEMPT_FAILED:
        /* Connected but not written to Flash is its own outcome (goal
         * node 10, failure paths). */
        out_info->setup_state = connected ? XIAOMIAO_WIFI_SETUP_SAVE_FAILED
                                          : XIAOMIAO_WIFI_SETUP_FAILED;
        out_info->setup_error = detail;
        break;
    case XIAOMIAO_WIFI_ATTEMPT_IDLE:
    default:
        out_info->setup_state = XIAOMIAO_WIFI_SETUP_IDLE;
        break;
    }

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Internal API used by the provisioning module                        */
/* ------------------------------------------------------------------ */

esp_err_t xiaomiao_wifi_service_provision_connect(const char *ssid, const char *password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!xiaomiao_wifi_provisioning_session_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strnlen(ssid, XIAOMIAO_WIFI_SSID_MAX + 1);
    const size_t password_len = (password != NULL) ? strnlen(password, XIAOMIAO_WIFI_PASSWORD_MAX + 1) : 0;

    if (ssid_len == 0 || ssid_len > XIAOMIAO_WIFI_SSID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password_len > XIAOMIAO_WIFI_PASSWORD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_password_is_supported(password_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_lock();
    if (s_attempt == XIAOMIAO_WIFI_ATTEMPT_RUNNING) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (!credentials_set(&s_trial, ssid, ssid_len, password, password_len)) {
        wifi_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    s_attempt = XIAOMIAO_WIFI_ATTEMPT_RUNNING;
    s_attempt_error = ESP_OK;
    s_user_disconnect = false;
    s_trial_switching = true;
    snapshot_set_state(XIAOMIAO_WIFI_CONNECTING);
    s_snapshot.last_error = ESP_OK;
    wifi_unlock();

    /*
     * esp_wifi_connect() refuses to run while an association exists, so
     * the current link has to be released first. Without this, switching
     * to another network could never succeed: the driver only warned
     * "sta is connected, disconnect before connecting to new ap" and the
     * trial sat in its connect timeout until it failed.
     */
    (void)esp_wifi_disconnect();

    /*
     * The leave is asynchronous and connect() is still refused while it
     * is in progress, so wait for the association to disappear rather
     * than guessing a delay. Bounded, because a station that never
     * associated has nothing to wait for.
     */
    for (int waited = 0; waited < WIFI_TRIAL_LEAVE_WAIT_MS; waited += 50) {
        wifi_ap_record_t current;
        memset(&current, 0, sizeof(current));
        if (esp_wifi_sta_get_ap_info(&current) != ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Trial credentials go to RAM only; the saved network in Flash is
     * not touched until an IPv4 lease arrives. */
    const esp_err_t err = station_apply_config(&s_trial);
    if (err != ESP_OK) {
        wifi_lock();
        s_trial_switching = false;
        s_attempt = XIAOMIAO_WIFI_ATTEMPT_FAILED;
        s_attempt_error = err;
        wifi_unlock();
        return err;
    }

    const esp_err_t conn_err = esp_wifi_connect();

    wifi_lock();
    s_trial_switching = false;
    if (conn_err != ESP_OK) {
        s_attempt = XIAOMIAO_WIFI_ATTEMPT_FAILED;
        s_attempt_error = conn_err;
    }
    wifi_unlock();

    if (conn_err != ESP_OK) {
        return conn_err;
    }

    timer_start_once(s_connect_timer, WIFI_CONNECT_TIMEOUT_US);
    ESP_LOGI(TAG, "trial connect started, ssid='%s' (password not logged)", s_trial.ssid);
    return ESP_OK;
}

xiaomiao_wifi_attempt_t xiaomiao_wifi_service_attempt_state(void)
{
    xiaomiao_wifi_attempt_t attempt;

    wifi_lock();
    attempt = s_attempt;
    wifi_unlock();

    return attempt;
}

esp_err_t xiaomiao_wifi_service_attempt_error(void)
{
    esp_err_t err;

    wifi_lock();
    err = s_attempt_error;
    wifi_unlock();

    return err;
}

void xiaomiao_wifi_service_attempt_reset(void)
{
    wifi_lock();
    if (s_attempt != XIAOMIAO_WIFI_ATTEMPT_RUNNING) {
        s_attempt = XIAOMIAO_WIFI_ATTEMPT_IDLE;
        s_attempt_error = ESP_OK;
    }
    wifi_unlock();
}
