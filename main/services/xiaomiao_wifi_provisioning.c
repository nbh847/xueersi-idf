/*
 * SoftAP + HTTP provisioning session (goal node 10).
 *
 * Owned by the Wi-Fi Service: the Service decides when a session starts
 * and when it ends, this module only moves the resources. A session
 * consists of
 *   1. a WPA2 SoftAP `Xiaomiao-XXXX` with one allowed client and a fresh
 *      eight-digit password,
 *   2. a DNS responder that points every lookup at the hotspot, so the
 *      phone offers its sign-in sheet,
 *   3. a small page that lists the scanned networks, takes an SSID and a
 *      password and reports the trial result,
 *   4. a ten-minute idle timer.
 *
 * Credential handling: the page never stores anything. The submitted
 * credentials travel to the Service, which keeps them in RAM, tries
 * them, and only writes Flash after an IPv4 lease arrived (goal node 10,
 * "Credential persistence").
 *
 * Teardown stops HTTP, DNS and AP DHCP before switching to station mode
 * and destroying the AP netif. Normal stop and failed-start rollback
 * both follow that order.
 */

#include "xiaomiao_wifi_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "xiaomiao_wifi_dns.h"

static const char TAG[] = "wifi_prov";

#define PROV_AP_SSID_PREFIX   "Xiaomiao-"
/* Fallback channel for a session that starts with no station link. */
#define PROV_AP_CHANNEL       1
#define PROV_IDLE_TIMEOUT_US  (10 * 60 * 1000 * 1000)
/* Hard limit of the submitted form; the page enforces it too. */
#define PROV_BODY_MAX         512
/* JSON buffer for the network list. */
#define PROV_JSON_MAX         4096

static SemaphoreHandle_t s_lock;
static bool s_active;
static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static esp_timer_handle_t s_idle_timer;
static char s_ap_ssid[XIAOMIAO_WIFI_SSID_BUF];
static char s_ap_password[XIAOMIAO_WIFI_AP_PASSWORD_BUF];

/* ------------------------------------------------------------------ */
/* Page                                                                */
/* ------------------------------------------------------------------ */

static const char PROV_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Xiaomiao Wi-Fi</title><style>"
    "body{font-family:sans-serif;margin:16px;background:#0e1016;color:#c8d0e0}"
    "h1{font-size:20px}div.net{padding:8px;margin:4px 0;background:#1b1f2a;"
    "border:1px solid #39404f;border-radius:4px}"
    "div.net.on{border-color:#2d6cdf;background:#243a63}"
    "input,button{font-size:16px;padding:6px;margin-top:8px;width:100%;box-sizing:border-box}"
    "#msg{margin-top:12px;color:#e0a030;min-height:20px}"
    "</style></head><body><h1>Xiaomiao Wi-Fi Setup</h1>"
    "<div id=\"msg\">Scanning...</div><div id=\"list\"></div>"
    "<form id=\"f\"><label>SSID</label>"
    "<input id=\"ssid\" readonly placeholder=\"select a network\">"
    "<label>Password</label>"
    "<input id=\"pwd\" type=\"password\" autocomplete=\"off\">"
    "<button type=\"submit\">Connect</button></form>"
    "<button id=\"again\">Rescan</button><script>"
    "var sel='';var R=document.getElementById('list');var M=document.getElementById('msg');"
    "function msg(t){M.textContent=t;}"
    "function render(n){R.innerHTML='';n.forEach(function(a){var d=document.createElement('div');"
    "d.className='net';d.textContent=a.ssid+' ('+a.rssi+' dBm'+(a.secure?')':', open)');"
    "d.onclick=function(){sel=a.ssid;document.getElementById('ssid').value=a.ssid;"
    "for(var i=0;i<R.children.length;i++){R.children[i].className='net';}"
    "d.className='net on';};R.appendChild(d);});}"
    "function scan(){msg('Scanning...');fetch('/api/scan',{method:'POST'}).then(wait);}"
    "function wait(){fetch('/api/scan').then(function(r){return r.json();}).then(function(j){"
    "if(!j.ready){setTimeout(wait,800);return;}render(j.nets);msg('Select a network, then enter its password.');});}"
    "function poll(){fetch('/api/status').then(function(r){return r.json();}).then(function(j){"
    "if(j.state==='connected'){msg('Connected. You can close this page.');return;}"
    "if(j.state==='failed'){msg('Failed: '+j.error);return;}"
    "if(j.state==='save_failed'){msg('Connected, but the credentials could not be saved.');return;}"
    "setTimeout(poll,1000);});}"
    "document.getElementById('f').onsubmit=function(e){e.preventDefault();"
    "if(!sel){msg('Select a network first.');return;}"
    "msg('Connecting...');"
    "fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "body:'ssid='+encodeURIComponent(sel)+'&password='+encodeURIComponent(document.getElementById('pwd').value)})"
    ".then(function(r){if(r.status===409){msg('A connection attempt is already running.');return;}"
    "if(r.status!==200){msg('Request rejected.');return;}poll();});};"
    "document.getElementById('again').onclick=scan;scan();"
    "</script></body></html>";

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

/* Any traffic or association proves the phone is still working with the
 * session, so the idle countdown restarts (goal node 10, "SoftAP"). */
static void touch_idle_timer(void)
{
    if (s_idle_timer == NULL) {
        return;
    }

    (void)esp_timer_stop(s_idle_timer);
    const esp_err_t err = esp_timer_start_once(s_idle_timer, PROV_IDLE_TIMEOUT_US);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "idle timer restart failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    }
}

static void idle_timer_cb(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "provisioning idle for ten minutes, closing the session");
    /* The Service owns the state around a session; asking it to stop
     * keeps success, cancel and timeout on one path. */
    (void)xiaomiao_wifi_provisioning_stop();
}

/* Eight digits from the hardware random source; a fresh password per
 * session, never an open hotspot (goal node 10, "SoftAP"). */
static void generate_ap_password(char *out, size_t out_len)
{
    uint8_t raw[8];
    esp_fill_random(raw, sizeof(raw));

    const size_t digits = out_len - 1;
    for (size_t i = 0; i < digits; ++i) {
        out[i] = (char)('0' + (raw[i] % 10));
    }
    out[digits] = '\0';
}

static void build_ap_ssid(char *out, size_t out_len)
{
    uint8_t mac[6] = { 0 };
    if (esp_wifi_get_mac(WIFI_IF_AP, mac) != ESP_OK) {
        /* Without a MAC the suffix is omitted rather than invented. */
        snprintf(out, out_len, PROV_AP_SSID_PREFIX);
        return;
    }

    snprintf(out, out_len, PROV_AP_SSID_PREFIX "%02X%02X", mac[4], mac[5]);
}

/* ------------------------------------------------------------------ */
/* JSON helpers                                                        */
/* ------------------------------------------------------------------ */

/*
 * Append a JSON string literal. The SSID is external data, so quotes,
 * backslashes and control bytes are escaped instead of copied verbatim
 * (goal node 10, "Concurrency, resources and logging").
 */
static size_t json_append_string(char *out, size_t capacity, size_t used, const char *value)
{
    if (used + 1 >= capacity) {
        return used;
    }

    out[used++] = '"';
    for (const char *p = value; *p != '\0' && used + 7 < capacity; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            out[used++] = '\\';
            out[used++] = (char)c;
        }
        else if (c < 0x20) {
            used += (size_t)snprintf(out + used, capacity - used, "\\u%04X", (unsigned)c);
        }
        else {
            out[used++] = (char)c;
        }
    }
    out[used++] = '"';
    out[used] = '\0';

    return used;
}

static size_t json_append(char *out, size_t capacity, size_t used, const char *text)
{
    if (used >= capacity) {
        return used;
    }

    const size_t written = (size_t)snprintf(out + used, capacity - used, "%s", text);
    used += (written < capacity - used) ? written : (capacity - used - 1);

    return used;
}

/* ------------------------------------------------------------------ */
/* Form decoding                                                       */
/* ------------------------------------------------------------------ */

static bool form_value(const char *body, const char *key, char *out, size_t out_len)
{
    const size_t key_len = strlen(key);
    const char *cursor = body;

    while (cursor != NULL && *cursor != '\0') {
        const char *next = strchr(cursor, '&');

        if (strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=') {
            const char *value = cursor + key_len + 1;
            const size_t value_len = (next != NULL) ? (size_t)(next - value) : strlen(value);

            size_t written = 0;
            for (size_t i = 0; i < value_len; ++i) {
                char c = value[i];

                if (c == '+') {
                    c = ' ';
                }
                else if (c == '%') {
                    if (i + 2 >= value_len) {
                        return false;
                    }
                    const char hi = value[i + 1];
                    const char lo = value[i + 2];
                    const int high = (hi >= '0' && hi <= '9')   ? hi - '0'
                                     : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
                                     : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                                                                : -1;
                    const int low = (lo >= '0' && lo <= '9')   ? lo - '0'
                                    : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
                                    : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                                                               : -1;
                    if (high < 0 || low < 0) {
                        return false;
                    }
                    c = (char)((high << 4) | low);
                    i += 2;
                }

                /* An overlong field is rejected, never silently cut: a
                 * truncated credential would be tried as if the user had
                 * typed it (goal node 10, "Web page"). */
                if (written + 1 >= out_len) {
                    return false;
                }
                out[written++] = c;
            }
            out[written] = '\0';

            return true;
        }

        cursor = (next != NULL) ? next + 1 : NULL;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                       */
/* ------------------------------------------------------------------ */

static esp_err_t handler_root(httpd_req_t *req)
{
    touch_idle_timer();
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PROV_PAGE, HTTPD_RESP_USE_STRLEN);
}

/*
 * Every unknown path lands on the page, so the captive-portal probes of
 * a phone resolve instead of showing a browser error.
 */
static esp_err_t handler_not_found(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;

    touch_idle_timer();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", XIAOMIAO_WIFI_PROVISIONING_URL "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handler_scan_post(httpd_req_t *req)
{
    touch_idle_timer();

    const esp_err_t err = xiaomiao_wifi_scan_start();
    /* A scan that is already running is not an error for the page. */
    const bool started = (err == ESP_OK) || (err == ESP_ERR_INVALID_STATE);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, started ? "{\"started\":true}" : "{\"started\":false}");
}

static esp_err_t handler_scan_get(httpd_req_t *req)
{
    touch_idle_timer();

    xiaomiao_wifi_ap_t networks[XIAOMIAO_WIFI_SCAN_MAX];
    memset(networks, 0, sizeof(networks));
    size_t count = 0;
    const esp_err_t err = xiaomiao_wifi_scan_get_results(networks, XIAOMIAO_WIFI_SCAN_MAX, &count);

    static char json[PROV_JSON_MAX];
    size_t used = 0;

    if (err != ESP_OK) {
        used = json_append(json, sizeof(json), used, "{\"ready\":false}");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, json, (ssize_t)used);
    }

    used = json_append(json, sizeof(json), used, "{\"ready\":true,\"nets\":[");
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            used = json_append(json, sizeof(json), used, ",");
        }
        used = json_append(json, sizeof(json), used, "{\"ssid\":");
        used = json_append_string(json, sizeof(json), used, networks[i].ssid);
        char tail[64];
        snprintf(tail, sizeof(tail), ",\"rssi\":%d,\"secure\":%s}", (int)networks[i].rssi,
                 networks[i].secure ? "true" : "false");
        used = json_append(json, sizeof(json), used, tail);
    }
    used = json_append(json, sizeof(json), used, "]}");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, (ssize_t)used);
}

/*
 * Read the submitted form. The declared length is bounded before a byte
 * is read, and a missing or overlong body is rejected instead of being
 * truncated into a half credential (goal node 10, "Web page").
 */
static esp_err_t handler_connect(httpd_req_t *req)
{
    touch_idle_timer();

    if (req->content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "empty body");
    }
    if (req->content_len > PROV_BODY_MAX) {
        httpd_resp_set_status(req, "413 Content Too Large");
        return httpd_resp_sendstr(req, "body too large");
    }

    char body[PROV_BODY_MAX + 1];
    size_t received = 0;
    while (received < (size_t)req->content_len) {
        const int chunk = httpd_req_recv(req, body + received, (size_t)req->content_len - received);
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (chunk <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "incomplete body");
        }
        received += (size_t)chunk;
    }
    body[received] = '\0';

    char ssid[XIAOMIAO_WIFI_SSID_BUF];
    char password[XIAOMIAO_WIFI_PASSWORD_BUF];
    if (!form_value(body, "ssid", ssid, sizeof(ssid)) ||
        !form_value(body, "password", password, sizeof(password))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "missing field");
    }

    const esp_err_t err = xiaomiao_wifi_service_provision_connect(ssid, password);
    httpd_resp_set_type(req, "application/json");

    switch (err) {
    case ESP_OK:
        return httpd_resp_sendstr(req, "{\"state\":\"connecting\"}");
    case ESP_ERR_INVALID_STATE:
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"state\":\"busy\"}");
    default:
        /* The reason is deliberately coarse: it must not become an
         * oracle for guessing a password. */
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"state\":\"rejected\"}");
    }
}

static esp_err_t handler_status(httpd_req_t *req)
{
    touch_idle_timer();

    const xiaomiao_wifi_attempt_t attempt = xiaomiao_wifi_service_attempt_state();
    const esp_err_t reason = xiaomiao_wifi_service_attempt_error();

    xiaomiao_wifi_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    (void)xiaomiao_wifi_get_snapshot(&snapshot);

    static char json[192];
    const char *name = "idle";
    const char *detail = "";

    switch (attempt) {
    case XIAOMIAO_WIFI_ATTEMPT_RUNNING:
        name = "connecting";
        break;
    case XIAOMIAO_WIFI_ATTEMPT_SUCCEEDED:
        name = "connected";
        break;
    case XIAOMIAO_WIFI_ATTEMPT_FAILED:
        if (snapshot.state == XIAOMIAO_WIFI_CONNECTED) {
            /* Connected, but the credential store rejected the write. */
            name = "save_failed";
        }
        else {
            name = "failed";
            switch (reason) {
            case ESP_ERR_INVALID_STATE:
                detail = "wrong password or authentication failed";
                break;
            case ESP_ERR_NOT_FOUND:
                detail = "network not found";
                break;
            case ESP_ERR_TIMEOUT:
                detail = "connection timed out";
                break;
            default:
                detail = "connection failed";
                break;
            }
        }
        break;
    case XIAOMIAO_WIFI_ATTEMPT_IDLE:
    default:
        name = "idle";
        detail = "no attempt yet";
        break;
    }

    snprintf(json, sizeof(json), "{\"state\":\"%s\",\"error\":\"%s\"}", name, detail);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* ------------------------------------------------------------------ */
/* Session lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void ap_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    switch (id) {
    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "AP client associated, internal heap: free=%u largest8bit=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        touch_idle_timer();
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        /* A phone that dropped the hotspot may come back; the countdown
         * restarts instead of ending the session (goal node 10, failure
         * paths). */
        touch_idle_timer();
        break;
    default:
        break;
    }
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* One phone is the whole audience; a small pool keeps the memory
     * footprint predictable. */
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    /* IDF 6.1 creates the HTTPD stack with
     * xTaskCreatePinnedToCoreWithCaps(), which multiplies this value by
     * sizeof(StackType_t): the allocation is 4x stack_size bytes of
     * contiguous internal RAM. 5120 asked for 20480 B while the largest
     * block at this point was 18432 B (device 2026-09-25, httpd_start
     * failed twice with ESP_ERR_HTTPD_TASK from a cold boot). 3584
     * reserves 14336 B, which still fits that hole and is ample for the
     * one-phone provisioning handlers. */
    config.stack_size = 3584;

    /* Record internal heap capacity around HTTPD startup. The old 5120
     * setting failed with 0xb008 on this device; the two largest-block
     * figures are diagnostics, not a standalone allocation verdict. */
    ESP_LOGI(TAG, "internal heap before httpd_start: free=%u largest=%u largest8bit=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s (0x%x), internal heap: free=%u largest=%u largest8bit=%u",
                 esp_err_to_name(err), (unsigned)err,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handler_root,
    };
    const httpd_uri_t scan_post = {
        .uri = "/api/scan",
        .method = HTTP_POST,
        .handler = handler_scan_post,
    };
    const httpd_uri_t scan_get = {
        .uri = "/api/scan",
        .method = HTTP_GET,
        .handler = handler_scan_get,
    };
    const httpd_uri_t connect = {
        .uri = "/api/connect",
        .method = HTTP_POST,
        .handler = handler_connect,
    };
    const httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = handler_status,
    };

    if (httpd_register_uri_handler(s_server, &root) != ESP_OK ||
        httpd_register_uri_handler(s_server, &scan_post) != ESP_OK ||
        httpd_register_uri_handler(s_server, &scan_get) != ESP_OK ||
        httpd_register_uri_handler(s_server, &connect) != ESP_OK ||
        httpd_register_uri_handler(s_server, &status) != ESP_OK ||
        httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, handler_not_found) !=
            ESP_OK) {
        ESP_LOGE(TAG, "registering the page handlers failed");
        (void)httpd_stop(s_server);
        s_server = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t xiaomiao_wifi_provisioning_session_start(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    lock();
    if (s_active) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_active = true;
    unlock();

    /* WPA DEBUG prints the temporary AP password and derived key material.
     * Keep these tags at INFO even if a local diagnostic sdkconfig still
     * permits DEBUG at compile time. */
    esp_log_level_set("wifi", ESP_LOG_INFO);
    esp_log_level_set("wpa", ESP_LOG_INFO);

    build_ap_ssid(s_ap_ssid, sizeof(s_ap_ssid));
    generate_ap_password(s_ap_password, sizeof(s_ap_password));

    esp_err_t err = ESP_OK;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "AP netif creation failed");
        err = ESP_FAIL;
        goto rollback;
    }

    /*
     * The SoftAP has to live on the station's channel: in APSTA the
     * driver forces the AP to follow the STA, and starting on a different
     * channel made it announce a channel switch right after a phone
     * joined, which dropped the phone with a 4-way handshake timeout
     * (reason 15, seen on hardware). Starting on the right channel
     * removes the switch altogether.
     */
    uint8_t channel = PROV_AP_CHANNEL;
    wifi_ap_record_t sta_info;
    memset(&sta_info, 0, sizeof(sta_info));
    if (esp_wifi_sta_get_ap_info(&sta_info) == ESP_OK && sta_info.primary != 0) {
        channel = sta_info.primary;
    }

    /*
     * The mode switch has to come before the AP configuration:
     * esp_wifi_set_config(WIFI_IF_AP, ...) is rejected with
     * ESP_ERR_WIFI_MODE unless the current mode already contains AP.
     * Switching to APSTA restarts the driver and drops the station
     * association; the Service puts the saved network back when the
     * session ends.
     */
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "APSTA mode failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
        goto rollback;
    }

    /*
     * Stop the station from connecting anywhere for the length of the
     * session. With no saved network the Service leaves the station
     * configured with an empty SSID and an OPEN threshold, which the driver
     * reads as "join any open access point": entering APSTA kicked off that
     * attempt in the same millisecond as the mode switch (device
     * 2026-09-25, "Haven't to connect to a suitable AP now!" right before
     * the mode line). That is worth cancelling on its own merits -- the
     * firmware should not join a stranger's open hotspot behind the user's
     * back -- but it is NOT the cause of the phone's reason-15 timeouts:
     * with this call in place the join still timed out after the same four
     * seconds (device 2026-09-25, next round). The Service reconnects the
     * saved network when the session ends, so nothing here needs the
     * station in the meantime.
     */
    {
        const esp_err_t drop_err = esp_wifi_disconnect();
        if (drop_err != ESP_OK) {
            ESP_LOGI(TAG, "no station attempt to cancel: %s (0x%x)", esp_err_to_name(drop_err),
                     (unsigned)drop_err);
        }
    }

    /*
     * No modem sleep while the page is being served: a sleeping radio
     * makes the AP slow to answer a joining phone, which showed up as
     * handshake timeouts during the first session. Restored at teardown.
     */
    (void)esp_wifi_set_ps(WIFI_PS_NONE);

    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    memcpy(ap_config.ap.ssid, s_ap_ssid, strlen(s_ap_ssid));
    ap_config.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    memcpy(ap_config.ap.password, s_ap_password, strlen(s_ap_password));
    ap_config.ap.channel = channel;
    ap_config.ap.max_connection = 1;
    /* A protected hotspot, never an open one. */
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AP configuration failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
        goto rollback;
    }

    /*
     * HT20 rather than the default 40 MHz: a provisioning hotspot needs
     * compatibility, not throughput. The phones associated as "bgn, 40U"
     * right before their 4-way handshake timeouts, and a 20 MHz channel
     * is the more forgiving of the two.
     */
    (void)esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);

    /*
     * Temporary diagnostics (goals/20260925-1120): the read-back says what
     * the AP actually runs with, which is the only trustworthy account left
     * after the stack size, the forced bandwidth and the RF recalibration
     * were each disproved on hardware. Only the password length is logged,
     * never the password.
     */
    {
        wifi_config_t applied;
        memset(&applied, 0, sizeof(applied));
        if (esp_wifi_get_config(WIFI_IF_AP, &applied) == ESP_OK) {
            ESP_LOGI(TAG,
                     "AP in effect: authmode=%d channel=%u ssid_len=%u pwd_len=%u max_conn=%u "
                     "pmf_required=%d",
                     (int)applied.ap.authmode, (unsigned)applied.ap.channel,
                     (unsigned)applied.ap.ssid_len,
                     (unsigned)strlen((const char *)applied.ap.password),
                     (unsigned)applied.ap.max_connection, (int)applied.ap.pmf_cfg.required);
        }
    }

    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &ap_event_handler,
                                     NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
                                         &ap_event_handler, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AP event registration failed: %s (0x%x)", esp_err_to_name(err),
                 (unsigned)err);
        goto rollback;
    }

    esp_ip4_addr_t ap_ip;
    ap_ip.addr = 0;
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        ap_ip = ip_info.ip;
    }
    else {
        /* The documented default address of the ESP-IDF AP netif. */
        ap_ip.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    }

    err = xiaomiao_wifi_dns_start(ap_ip);
    if (err != ESP_OK) {
        goto rollback;
    }

    err = start_http_server();
    if (err != ESP_OK) {
        goto rollback_dns;
    }
    ESP_LOGI(TAG, "internal heap after httpd_start: free=%u largest8bit=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    if (s_idle_timer == NULL) {
        const esp_timer_create_args_t idle_args = {
            .callback = idle_timer_cb,
            .name = "wifi_prov_idle",
        };
        err = esp_timer_create(&idle_args, &s_idle_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "idle timer creation failed: %s (0x%x)", esp_err_to_name(err),
                     (unsigned)err);
            goto rollback_http;
        }
    }
    touch_idle_timer();

    ESP_LOGI(TAG, "session ready, ssid='%s' (temporary password shown on screen only)",
             s_ap_ssid);
    return ESP_OK;

rollback_http:
    (void)httpd_stop(s_server);
    s_server = NULL;
rollback_dns:
    xiaomiao_wifi_dns_stop();
rollback:
    /*
     * Nothing half-started survives a failed start. Unregistering an
     * handler that was never registered only reports NOT_FOUND, which is
     * exactly what a rollback wants, so both are always attempted.
     */
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &ap_event_handler);
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
                                       &ap_event_handler);
    esp_log_level_set("wifi", ESP_LOG_INFO);
    esp_log_level_set("wpa", ESP_LOG_INFO);
    if (s_ap_netif != NULL) {
        const esp_err_t dhcp_err = esp_netif_dhcps_stop(s_ap_netif);
        if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "stopping AP DHCP server during rollback failed: %s (0x%x)",
                     esp_err_to_name(dhcp_err), (unsigned)dhcp_err);
        }
    }
    (void)esp_wifi_set_mode(WIFI_MODE_STA);
    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    lock();
    s_active = false;
    unlock();
    /* The temporary password is useless once the session is gone. */
    memset(s_ap_password, 0, sizeof(s_ap_password));
    memset(s_ap_ssid, 0, sizeof(s_ap_ssid));

    /* Back to the firmware default: modem sleep on. */
    (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return err;
}

esp_err_t xiaomiao_wifi_provisioning_session_stop(void)
{
    if (s_lock == NULL) {
        return ESP_OK;
    }

    lock();
    const bool was_active = s_active;
    s_active = false;
    unlock();

    if (!was_active) {
        return ESP_OK;
    }

    if (s_idle_timer != NULL) {
        (void)esp_timer_stop(s_idle_timer);
    }

    /* Release the page and DNS before stopping AP networking. */
    if (s_server != NULL) {
        (void)httpd_stop(s_server);
        s_server = NULL;
    }

    xiaomiao_wifi_dns_stop();

    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &ap_event_handler);
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
                                       &ap_event_handler);

    /* AP_STOP is handled asynchronously. Stop DHCP while its netif still
     * exists so destroying the netif cannot leave a UDP/67 server behind. */
    if (s_ap_netif != NULL) {
        const esp_err_t dhcp_err = esp_netif_dhcps_stop(s_ap_netif);
        if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "stopping AP DHCP server failed: %s (0x%x)",
                     esp_err_to_name(dhcp_err), (unsigned)dhcp_err);
        }
    }

    /* Back to station-only operation; the Service reconnects the saved
     * network afterwards. */
    const esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }

    memset(s_ap_password, 0, sizeof(s_ap_password));
    memset(s_ap_ssid, 0, sizeof(s_ap_ssid));

    /* Back to the firmware default: modem sleep on. */
    (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    /* Leak probe (device 2026-09-25): the second session failed in
     * httpd_start with 3828 bytes less internal free than the first,
     * same largest block. These numbers across two sessions say how
     * much a full open/close cycle really retains. */
    ESP_LOGI(TAG, "session resources released, internal heap now: free=%u largest=%u largest8bit=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    return err;
}

bool xiaomiao_wifi_provisioning_session_active(void)
{
    bool active = false;

    lock();
    active = s_active;
    unlock();

    return active;
}

esp_err_t xiaomiao_wifi_provisioning_session_info(xiaomiao_wifi_provisioning_info_t *out_info)
{
    if (out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));

    lock();
    if (!s_active) {
        unlock();
        return ESP_OK;
    }

    out_info->active = true;
    memcpy(out_info->ssid, s_ap_ssid, sizeof(out_info->ssid));
    memcpy(out_info->password, s_ap_password, sizeof(out_info->password));
    snprintf(out_info->url, sizeof(out_info->url), "%s", XIAOMIAO_WIFI_PROVISIONING_URL);
    unlock();

    uint64_t expiry = 0;
    if (s_idle_timer != NULL && esp_timer_get_expiry_time(s_idle_timer, &expiry) == ESP_OK) {
        const uint64_t now = (uint64_t)esp_timer_get_time();
        out_info->remaining_s = (expiry > now) ? (uint32_t)((expiry - now) / 1000000ULL) : 0;
    }

    return ESP_OK;
}
