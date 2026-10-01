/*
 * Time Service implementation (goal 20261001-1657, CP1).
 *
 * Ownership and thread model:
 * - The SNTP sync callback runs in the lwIP tcpip thread. It only
 *   validates the delivered time and updates the protected sync
 *   metadata inside a short critical section; no LVGL, no formatting,
 *   no network I/O.
 * - poll() runs on the UI thread at 1 Hz and performs the network
 *   state machine with non-waiting control calls only.
 * - get_snapshot() reads the sync metadata and the monotonic clock
 *   under the same critical section, then samples the wall time and
 *   formats outside the lock. ESP-IDF v6.1 SNTP (IMMED mode) calls
 *   settimeofday() before the sync callback, so a wall-time sample
 *   taken after the metadata read is never older than the sync point.
 *
 * ESP-IDF v6.1 facts this relies on (checked against release/v6.1
 * sources, CP0):
 * - esp_netif_sntp_init() registers the sync callback and creates the
 *   SNTP instance; with config.start = false it stays stopped until
 *   esp_netif_sntp_start().
 * - esp_netif_sntp_deinit() stops SNTP, unregisters the callback and
 *   frees the storage; it is safe when nothing is initialized.
 * - lwIP SNTP rotates to the next configured server on failure and
 *   doubles its retry timeout up to ten times; the periodic re-sync
 *   interval is CONFIG_LWIP_SNTP_UPDATE_DELAY. No custom retry worker
 *   is added here.
 */

#include "xiaomiao_time_service.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "esp_netif_sntp.h"

#include "xiaomiao_wifi_service.h"

static const char *TAG = "time";

/* Validity window and cadences (goal "确定的实现决策"). */
#define TIME_VALID_WINDOW_US   (24LL * 3600LL * 1000000LL)
#define TIME_POLL_INTERVAL_US  (1000000LL)
#define TIME_RETRY_INTERVAL_US (30LL * 1000000LL)

/* NTP servers, compiled-in candidates; reachability on the target
 * network is not guaranteed by either name (design, CP0). */
#define TIME_SERVER_0 "ntp.aliyun.com"
#define TIME_SERVER_1 "pool.ntp.org"

/* Legal UTC seconds window [2026-01-01, 2100-01-01). Plain SNTP is a
 * display source only; the range rejects 1970-era clocks and obvious
 * garbage, it is not an authenticity check. */
#define TIME_UTC_MIN_SEC 1767225600LL /* 2026-01-01T00:00:00Z */
#define TIME_UTC_MAX_SEC 4102444800LL /* 2100-01-01T00:00:00Z */

#define TIME_BEIJING_OFFSET_SEC (8LL * 3600LL)

typedef struct {
    portMUX_TYPE lock;
    bool inited;
    esp_err_t init_err;      /* first init() result, kept for snapshot */
    bool sntp_created;       /* esp_netif_sntp instance exists */
    bool sntp_started;       /* instance is running */
    uint32_t active_ipv4;    /* lease the running instance was built for */
    bool sync_valid;         /* last sync was legal (this boot only) */
    int64_t last_sync_us;    /* monotonic time of the last legal sync */
    int64_t next_poll_us;    /* 1 Hz throttle */
    int64_t retry_deadline_us; /* init/start retry gate */
    bool reported_valid;     /* validity-change logging edge */
} time_service_t;

static time_service_t s_ts = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .inited = false,
    .init_err = ESP_OK,
    .sntp_created = false,
    .sntp_started = false,
    .active_ipv4 = 0,
    .sync_valid = false,
    .last_sync_us = 0,
    .next_poll_us = 0,
    .retry_deadline_us = 0,
    .reported_valid = false,
};

/* ------------------------------------------------------------------
 * SNTP sync callback (lwIP tcpip thread)
 * ------------------------------------------------------------------
 * tv comes straight from the NTP response that lwIP is about to apply
 * (it has already been applied in IMMED mode before this runs), so it
 * is used directly and never re-read from the system clock.
 */
static void time_sync_cb(struct timeval *tv)
{
    bool legal = false;

    if (tv != NULL) {
        const int64_t sec = (int64_t)tv->tv_sec;
        legal = (sec >= TIME_UTC_MIN_SEC) && (sec < TIME_UTC_MAX_SEC) &&
                (tv->tv_usec >= 0) && (tv->tv_usec <= 999999);
    }

    portENTER_CRITICAL(&s_ts.lock);
    if (legal) {
        s_ts.sync_valid = true;
        s_ts.last_sync_us = esp_timer_get_time();
    } else {
        /* An illegal sync poisons the source: hide the clock until the
         * next legal sync restores it. */
        s_ts.sync_valid = false;
    }
    portEXIT_CRITICAL(&s_ts.lock);

    if (!legal) {
        ESP_LOGW(TAG, "rejected illegal SNTP sync");
    } else {
        ESP_LOGI(TAG, "SNTP sync accepted");
    }
}

/* Create the stopped SNTP instance. Used by init() and by poll()
 * retries; never starts the service itself. */
static esp_err_t time_sntp_create(void)
{
    esp_sntp_config_t config = {
        .smooth_sync = false,             /* IMMED: jump straight to NTP time */
        .server_from_dhcp = false,
        .wait_for_sync = false,           /* no semaphore, no waiting API */
        .start = false,                   /* poll() starts it when online */
        .sync_cb = time_sync_cb,
        .renew_servers_after_new_IP = false,
        .ip_event_to_renew = IP_EVENT_STA_GOT_IP,
        .index_of_first_server = 0,
        .num_of_servers = 2,
        .servers = { TIME_SERVER_0, TIME_SERVER_1 },
    };

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        return err;
    }
    s_ts.sntp_created = true;
    return ESP_OK;
}

/* Release the SNTP instance, if any. The sync validity window is
 * deliberately kept: display continuity runs on the monotonic clock. */
static void time_sntp_destroy(void)
{
    if (s_ts.sntp_created) {
        esp_netif_sntp_deinit();
        s_ts.sntp_created = false;
        s_ts.sntp_started = false;
    }
}

esp_err_t xiaomiao_time_service_init(void)
{
    if (s_ts.inited) {
        return s_ts.init_err;
    }
    s_ts.inited = true;

    /* Registering the sync notification and creating the stopped
     * instance do not need the network; the start waits for poll(). */
    s_ts.init_err = time_sntp_create();
    if (s_ts.init_err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed: %s (0x%x), will retry in poll",
                 esp_err_to_name(s_ts.init_err), (unsigned)s_ts.init_err);
        s_ts.retry_deadline_us = esp_timer_get_time() + TIME_RETRY_INTERVAL_US;
    }

    return s_ts.init_err;
}

/* ------------------------------------------------------------------
 * Network state machine (UI thread, 1 Hz)
 * ------------------------------------------------------------------
 */

void xiaomiao_time_service_poll(void)
{
    if (!s_ts.inited) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (now < s_ts.next_poll_us) {
        return;
    }
    s_ts.next_poll_us = now + TIME_POLL_INTERVAL_US;

    /* Read the replicated Wi-Fi snapshot; every failure or non-connected
     * state is treated as "no network". */
    xiaomiao_wifi_snapshot_t wifi;
    bool net_ok = false;
    uint32_t ipv4 = 0;
    if (xiaomiao_wifi_get_snapshot(&wifi) == ESP_OK &&
        wifi.state == XIAOMIAO_WIFI_CONNECTED) {
        ipv4 = (uint32_t)wifi.ipv4.addr;
        net_ok = (ipv4 != 0);
    }

    if (net_ok) {
        /* Recover the instance first: after a teardown (network loss,
         * failed start) there is nothing to start. */
        if (!s_ts.sntp_created && now < s_ts.retry_deadline_us) {
            return; /* failure retry gate */
        }
        if (!s_ts.sntp_created) {
            if (time_sntp_create() != ESP_OK) {
                ESP_LOGW(TAG, "SNTP re-init failed, retry in 30 s");
                s_ts.retry_deadline_us = now + TIME_RETRY_INTERVAL_US;
                return;
            }
            s_ts.init_err = ESP_OK;
            ESP_LOGI(TAG, "SNTP init recovered");
        }

        if (!s_ts.sntp_started) {
            const esp_err_t err = esp_netif_sntp_start();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "SNTP start failed: %s (0x%x)",
                         esp_err_to_name(err), (unsigned)err);
                /* Recreate the instance on the next retry window. */
                time_sntp_destroy();
                s_ts.retry_deadline_us = now + TIME_RETRY_INTERVAL_US;
            } else {
                s_ts.sntp_started = true;
                s_ts.active_ipv4 = ipv4;
                ESP_LOGI(TAG, "SNTP started");
            }
        } else if (ipv4 != s_ts.active_ipv4) {
            /* One rebuild per IP change; the stale instance keeps
             * polling through a socket bound to the old world. */
            time_sntp_destroy();
            if (time_sntp_create() == ESP_OK &&
                esp_netif_sntp_start() == ESP_OK) {
                s_ts.sntp_started = true;
                s_ts.active_ipv4 = ipv4;
                ESP_LOGI(TAG, "SNTP rebuilt for new IP");
            } else {
                time_sntp_destroy();
                s_ts.retry_deadline_us = now + TIME_RETRY_INTERVAL_US;
            }
        }
    } else {
        /* No usable network: release the SNTP instance. The display
         * keeps running off the system clock inside the 24 h window. */
        time_sntp_destroy();
    }

    /* Log validity edges only (never per second). */
    bool valid = false;
    portENTER_CRITICAL(&s_ts.lock);
    valid = s_ts.sync_valid &&
            (now - s_ts.last_sync_us) < TIME_VALID_WINDOW_US;
    portEXIT_CRITICAL(&s_ts.lock);
    if (valid != s_ts.reported_valid) {
        s_ts.reported_valid = valid;
        ESP_LOGI(TAG, "clock display %s", valid ? "valid" : "invalid");
    }
}

/* ------------------------------------------------------------------
 * Snapshot
 * ------------------------------------------------------------------
 */

esp_err_t xiaomiao_time_get_snapshot(xiaomiao_time_snapshot_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Every early return leaves a cleared, explicitly invalid output. */
    out->valid = false;
    out->datetime[0] = '\0';

    if (!s_ts.inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ts.init_err != ESP_OK) {
        return s_ts.init_err;
    }

    /* Metadata first (protected), wall time second: the sample can
     * only be newer than the recorded sync point. */
    bool sync_valid;
    int64_t last_sync_us;
    portENTER_CRITICAL(&s_ts.lock);
    sync_valid = s_ts.sync_valid;
    last_sync_us = s_ts.last_sync_us;
    portEXIT_CRITICAL(&s_ts.lock);

    if (!sync_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now_mono = esp_timer_get_time();
    if ((now_mono - last_sync_us) >= TIME_VALID_WINDOW_US) {
        return ESP_ERR_INVALID_STATE;
    }

    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* The system wall clock itself must be sane before it is shown. */
    const int64_t utc_sec = (int64_t)tv.tv_sec;
    if (utc_sec < TIME_UTC_MIN_SEC || utc_sec >= TIME_UTC_MAX_SEC) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Fixed UTC+8 for display; the global TZ stays untouched. */
    time_t beijing = (time_t)(utc_sec + TIME_BEIJING_OFFSET_SEC);
    struct tm tm_beijing;
    if (gmtime_r(&beijing, &tm_beijing) == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const int written =
        snprintf(out->datetime, sizeof(out->datetime), "%04d-%02d-%02d %02d:%02d",
                 tm_beijing.tm_year + 1900, tm_beijing.tm_mon + 1,
                 tm_beijing.tm_mday, tm_beijing.tm_hour, tm_beijing.tm_min);
    if (written != (int)(XIAOMIAO_TIME_DATETIME_BUF - 1)) {
        out->datetime[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    out->valid = true;
    return ESP_OK;
}
