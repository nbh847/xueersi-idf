/*
 * Agent Service implementation (goal node 11).
 *
 * Responsibilities, in the order they appear below:
 *   1. address configuration - IPv4 text validation and the URL
 *   2. HTTP fetch            - bounded read of one Agent response
 *   3. JSON validation       - the frozen API v1 contract
 *   4. snapshot              - locked, self-consistent view for the UI
 *   5. worker                - the one background loop that owns HTTP
 *
 * Threading rules (goal node 11, "Worker scheduling"): network I/O,
 * JSON parsing and logging all happen outside the Service lock; the
 * lock only guards the commit of a parsed snapshot and the copy in
 * get_snapshot(). The Worker never calls esp_wifi_* - Wi-Fi readiness
 * comes from the Wi-Fi Service public snapshot, which stays the single
 * source of connectivity truth.
 *
 * Failure discipline (goal node 11, "Failure paths"): a response only
 * commits when the schema matches v1, status is ok or degraded, the
 * core CPU/memory fields are finite numbers inside 0..100 and the
 * reported age is below 3 seconds. Optional GPU/temperature fields
 * degrade independently; everything else is an all-or-nothing round.
 * The 3-second display rule lives in get_snapshot(): past that window
 * the valid flags read false and the PC Monitor shows `--` again.
 */

#include "xiaomiao_agent_service.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "xiaomiao_wifi_service.h"

static const char TAG[] = "agent_svc";

/* `http://` + longest IPv4 + `:port` + the metrics path + terminator. */
#define AGENT_URL_MAX 64
/* Hard limit of one response body; anything longer is rejected whole. */
#define AGENT_RESPONSE_MAX 2048
/* The display validity window: past this, valid flags read false. */
#define AGENT_VALIDITY_US (3 * 1000 * 1000)
/* A response this old on arrival is not a new valid snapshot. */
#define AGENT_MAX_RESPONSE_AGE_SEC 3.0
/* Period after a valid fetch, and the observation period while the
 * Wi-Fi Service has no IPv4 yet. Failures wait at least 2 seconds to
 * avoid a busy loop against a stopped Agent (goal node 11, "Worker
 * scheduling"). */
#define AGENT_POLL_ONLINE_MS 1000
#define AGENT_POLL_OFFLINE_MS 1000
#define AGENT_RETRY_WAIT_MS 2000
/* One read/write timeout covers the whole request; ESP-IDF 6.1
 * esp_http_client has no separate connect timeout knob, so 1500 ms is
 * the documented total budget (goal node 11, "Worker scheduling"). */
#define AGENT_HTTP_TIMEOUT_MS 1500
/* Internal RAM is tight since the Wi-Fi Service landed (goal node 10);
 * 4 KB holds the 2 KB body buffer plus the HTTP client state. The
 * stack high watermark is recorded during the on-device validation. */
#define AGENT_TASK_STACK_BYTES 4096
#define AGENT_TASK_PRIORITY 3

/* Validity windows shared by the percent and temperature fields. */
#define AGENT_PERCENT_MIN 0.0f
#define AGENT_PERCENT_MAX 100.0f
#define AGENT_TEMPERATURE_MIN (-100.0f)
#define AGENT_TEMPERATURE_MAX 150.0f

/*
 * One committed metrics round. The core CPU/memory fields are always
 * valid here (a round without them never commits); only the optional
 * fields carry their own flags (goal decision 7).
 */
typedef struct {
    bool gpu_valid;
    bool cpu_temperature_valid;
    bool gpu_temperature_valid;
    float cpu_percent;
    float memory_percent;
    float gpu_percent;
    float cpu_temperature_c;
    float gpu_temperature_c;
    int64_t committed_us;
} agent_metrics_t;

static SemaphoreHandle_t s_lock;

static bool s_initialized;
static esp_err_t s_init_result = ESP_OK;

/* Written once by init() before the Worker exists, read-only after. */

static char s_metrics_url[AGENT_URL_MAX];

/* Protected by s_lock. */
static xiaomiao_agent_state_t s_state = XIAOMIAO_AGENT_UNINITIALIZED;
static agent_metrics_t s_metrics;
static bool s_has_committed;
static uint32_t s_consecutive_failures;
static int s_http_status;
static esp_err_t s_last_error = ESP_OK;

/* ------------------------------------------------------------------ */
/* 1. Address configuration                                            */
/* ------------------------------------------------------------------ */

/*
 * The build configuration must carry bare IPv4 text: four decimal
 * groups 0..255 separated by dots, nothing else (goal node 11, "Fixed
 * address configuration"). Anything else keeps the Service in
 * UNCONFIGURED instead of failing the boot.
 */
static bool agent_host_is_ipv4(const char *host)
{
    if (host == NULL || host[0] == '\0') {
        return false;
    }

    size_t index = 0;
    for (int group = 0; group < 4; ++group) {
        if (group > 0) {
            if (host[index] != '.') {
                return false;
            }
            ++index;
        }
        int value = 0;
        int digits = 0;
        while (host[index] >= '0' && host[index] <= '9') {
            value = value * 10 + (host[index] - '0');
            ++digits;
            ++index;
            if (digits > 3) {
                return false;
            }
        }
        if (digits == 0 || value > 255) {
            return false;
        }
        /* No leading zeros, matching the usual IPv4 text form. */
        if (digits > 1 && host[index - digits] == '0') {
            return false;
        }
    }
    return host[index] == '\0';
}

static bool agent_build_url(void)
{
    const int written = snprintf(s_metrics_url, sizeof(s_metrics_url),
                                 "http://%s:%d/api/v1/pc/metrics",
                                 CONFIG_XIAOMIAO_AGENT_HOST,
                                 CONFIG_XIAOMIAO_AGENT_PORT);
    return written > 0 && written < (int)sizeof(s_metrics_url);
}

/* ------------------------------------------------------------------ */
/* 2. HTTP fetch                                                       */
/* ------------------------------------------------------------------ */

/*
 * Fetch one response body into the fixed buffer. Every exit path
 * releases the client (goal node 11, checkpoint 2). Returns false when
 * the connection, the status line or the size limit rejects the
 * response; *out_status keeps the HTTP code for the snapshot, 0 when
 * the request never completed.
 */
static bool agent_fetch(char *body, size_t capacity, size_t *out_len, int *out_status)
{
    bool ok = false;
    *out_status = 0;
    *out_len = 0;

    const esp_http_client_config_t config = {
        .url = s_metrics_url,
        .timeout_ms = AGENT_HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "http client init failed");
        return false;
    }

    do {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGD(TAG, "connect to agent failed");
            break;
        }
        if (esp_http_client_fetch_headers(client) < 0) {
            break;
        }
        const int status = esp_http_client_get_status_code(client);
        *out_status = status;
        if (status != 200) {
            ESP_LOGD(TAG, "agent answered http %d", status);
            break;
        }
        const int content_length = esp_http_client_get_content_length(client);
        if (content_length > 0 && (size_t)content_length > capacity - 1) {
            ESP_LOGW(TAG, "agent response too large (%d bytes)", content_length);
            break;
        }

        size_t total = 0;
        bool transport_error = false;
        while (total < capacity - 1) {
            const int read_len = esp_http_client_read(client, body + total,
                                                      (int)(capacity - 1 - total));
            if (read_len < 0) {
                transport_error = true;
                break;
            }
            if (read_len == 0) {
                break;
            }
            total += (size_t)read_len;
        }
        if (transport_error || total >= capacity - 1) {
            ESP_LOGW(TAG, "agent response missing or too large");
            break;
        }
        body[total] = '\0';
        *out_len = total;
        ok = true;
    } while (false);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* ------------------------------------------------------------------ */
/* 3. JSON validation                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    AGENT_PARSE_FAILED = 0,   /* schema or transport problem: whole round fails */
    AGENT_PARSE_REJECTED,     /* server truthfully reports no fresh data */
    AGENT_PARSE_COMMITTED,    /* valid core snapshot */
} agent_parse_result_t;

/*
 * Validate one optional number. Missing, JSON null, a wrong type, a
 * non-finite value or an out-of-window value all mean "unavailable"
 * for that optional field only (goal node 11, "Failure paths").
 */
static bool agent_optional_number(const cJSON *data, const char *key,
                                  float minimum, float maximum, float *out_value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(data, key);
    if (item == NULL || cJSON_IsNull(item) || !cJSON_IsNumber(item)) {
        return false;
    }
    const float value = (float)item->valuedouble;
    if (!isfinite(value) || value < minimum || value > maximum) {
        return false;
    }
    *out_value = value;
    return true;
}

static agent_parse_result_t agent_parse_metrics(const char *body, agent_metrics_t *out)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return AGENT_PARSE_FAILED;
    }

    agent_parse_result_t result = AGENT_PARSE_FAILED;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    const cJSON *age = cJSON_GetObjectItemCaseSensitive(root, "age_sec");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    do {
        if (!cJSON_IsNumber(schema) || schema->valuedouble != 1.0) {
            break;
        }
        if (!cJSON_IsString(status) || status->valuestring == NULL) {
            break;
        }
        if (strcmp(status->valuestring, "unavailable") == 0 ||
            strcmp(status->valuestring, "stale") == 0) {
            /* The Agent keeps its own freshness contract (goal node 11,
             * API v1); such a response is true and useful, but it never
             * commits a snapshot or refreshes the success time. */
            result = AGENT_PARSE_REJECTED;
            break;
        }
        if (strcmp(status->valuestring, "ok") != 0 &&
            strcmp(status->valuestring, "degraded") != 0) {
            break;
        }
        if (!cJSON_IsNumber(age) || !isfinite((float)age->valuedouble) ||
            age->valuedouble < 0.0 || age->valuedouble >= AGENT_MAX_RESPONSE_AGE_SEC) {
            break;
        }
        if (!cJSON_IsObject(data)) {
            break;
        }

        /* The core fields are all-or-nothing (goal node 11, "Failure
         * paths"): missing, null, non-numeric, non-finite or out of
         * range fail the whole round without committing anything. */
        const cJSON *cpu = cJSON_GetObjectItemCaseSensitive(data, "cpu_percent");
        const cJSON *memory = cJSON_GetObjectItemCaseSensitive(data, "memory_percent");
        if (!cJSON_IsNumber(cpu) || !cJSON_IsNumber(memory)) {
            break;
        }
        const float cpu_value = (float)cpu->valuedouble;
        const float memory_value = (float)memory->valuedouble;
        if (!isfinite(cpu_value) || !isfinite(memory_value)) {
            break;
        }
        if (cpu_value < AGENT_PERCENT_MIN || cpu_value > AGENT_PERCENT_MAX ||
            memory_value < AGENT_PERCENT_MIN || memory_value > AGENT_PERCENT_MAX) {
            break;
        }

        memset(out, 0, sizeof(*out));
        out->cpu_percent = cpu_value;
        out->memory_percent = memory_value;
        out->gpu_valid = agent_optional_number(data, "gpu_percent",
                                               AGENT_PERCENT_MIN, AGENT_PERCENT_MAX,
                                               &out->gpu_percent);
        out->cpu_temperature_valid = agent_optional_number(data, "cpu_temperature_c",
                                                           AGENT_TEMPERATURE_MIN,
                                                           AGENT_TEMPERATURE_MAX,
                                                           &out->cpu_temperature_c);
        out->gpu_temperature_valid = agent_optional_number(data, "gpu_temperature_c",
                                                           AGENT_TEMPERATURE_MIN,
                                                           AGENT_TEMPERATURE_MAX,
                                                           &out->gpu_temperature_c);
        result = AGENT_PARSE_COMMITTED;
    } while (false);

    cJSON_Delete(root);
    return result;
}

/* ------------------------------------------------------------------ */
/* 4. Snapshot                                                         */
/* ------------------------------------------------------------------ */

static void agent_lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void agent_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void agent_set_state(xiaomiao_agent_state_t state)
{
    agent_lock();
    s_state = state;
    agent_unlock();
}

static void agent_note_failure(int http_status, esp_err_t error)
{
    agent_lock();
    ++s_consecutive_failures;
    s_http_status = http_status;
    s_last_error = error;
    agent_unlock();
}

static void agent_commit(const agent_metrics_t *metrics)
{
    agent_lock();
    s_metrics = *metrics;
    s_has_committed = true;
    s_consecutive_failures = 0;
    s_http_status = 200;
    s_last_error = ESP_OK;
    s_state = (metrics->gpu_valid && metrics->cpu_temperature_valid &&
               metrics->gpu_temperature_valid)
                  ? XIAOMIAO_AGENT_ONLINE
                  : XIAOMIAO_AGENT_DEGRADED;
    agent_unlock();
}

/* ------------------------------------------------------------------ */
/* 5. Worker                                                           */
/* ------------------------------------------------------------------ */

static bool agent_wifi_is_online(void)
{
    xiaomiao_wifi_snapshot_t wifi;
    if (xiaomiao_wifi_get_snapshot(&wifi) != ESP_OK) {
        return false;
    }
    return wifi.state == XIAOMIAO_WIFI_CONNECTED && wifi.ipv4.addr != 0;
}

static void agent_worker_task(void *argument)
{
    (void)argument;
    static char body[AGENT_RESPONSE_MAX];

    for (;;) {
        if (!agent_wifi_is_online()) {
            /* No IPv4 means no route to the Agent; the 3-second rule in
             * get_snapshot() retires the display on its own, so nothing
             * else to clear here (goal node 11, failure paths). */
            agent_set_state(XIAOMIAO_AGENT_WIFI_OFFLINE);
            vTaskDelay(pdMS_TO_TICKS(AGENT_POLL_OFFLINE_MS));
            continue;
        }

        agent_set_state(XIAOMIAO_AGENT_CONNECTING);

        size_t body_len = 0;
        int http_status = 0;
        const bool fetched = agent_fetch(body, sizeof(body), &body_len, &http_status);

        bool success = false;
        if (fetched) {
            agent_metrics_t parsed;
            const agent_parse_result_t parsed_result = agent_parse_metrics(body, &parsed);
            if (parsed_result == AGENT_PARSE_COMMITTED) {
                parsed.committed_us = esp_timer_get_time();
                agent_commit(&parsed);
                success = true;
            } else if (parsed_result == AGENT_PARSE_REJECTED) {
                /* Truthful "no fresh data": wait like a failure but keep
                 * the last committed snapshot and its success time. */
                agent_note_failure(http_status, ESP_ERR_INVALID_STATE);
            } else {
                agent_note_failure(http_status, ESP_ERR_INVALID_RESPONSE);
            }
        } else {
            agent_note_failure(http_status, ESP_FAIL);
        }

        if (success) {
            vTaskDelay(pdMS_TO_TICKS(AGENT_POLL_ONLINE_MS));
        } else {
            agent_set_state(XIAOMIAO_AGENT_RETRY_WAIT);
            vTaskDelay(pdMS_TO_TICKS(AGENT_RETRY_WAIT_MS));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public interface                                                    */
/* ------------------------------------------------------------------ */

esp_err_t xiaomiao_agent_service_init(void)
{
    if (s_initialized) {
        return s_init_result;
    }
    s_initialized = true;

    if (!agent_host_is_ipv4(CONFIG_XIAOMIAO_AGENT_HOST)) {
        /* An empty address is the documented offline mode; a malformed
         * one is logged once and treated the same way (goal node 11,
         * "Fixed address configuration"). */
        if (CONFIG_XIAOMIAO_AGENT_HOST[0] != '\0') {
            ESP_LOGW(TAG, "agent host '%s' is not IPv4 text, staying unconfigured",
                     CONFIG_XIAOMIAO_AGENT_HOST);
        } else {
            ESP_LOGI(TAG, "agent host not configured, staying unconfigured");
        }
        s_state = XIAOMIAO_AGENT_UNCONFIGURED;
        s_init_result = ESP_OK;
        return s_init_result;
    }

    if (!agent_build_url()) {
        ESP_LOGW(TAG, "agent URL does not fit the buffer, staying unconfigured");
        s_state = XIAOMIAO_AGENT_UNCONFIGURED;
        s_init_result = ESP_OK;
        return s_init_result;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        s_state = XIAOMIAO_AGENT_ERROR;
        s_last_error = ESP_ERR_NO_MEM;
        s_init_result = ESP_ERR_NO_MEM;
        return s_init_result;
    }

    if (xTaskCreate(agent_worker_task, "agent_svc", AGENT_TASK_STACK_BYTES,
                    NULL, AGENT_TASK_PRIORITY, NULL) != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_state = XIAOMIAO_AGENT_ERROR;
        s_last_error = ESP_ERR_NO_MEM;
        s_init_result = ESP_ERR_NO_MEM;
        return s_init_result;
    }


    s_state = XIAOMIAO_AGENT_WIFI_OFFLINE;
    ESP_LOGI(TAG, "agent service ready, url=%s", s_metrics_url);
    s_init_result = ESP_OK;
    return s_init_result;
}

esp_err_t xiaomiao_agent_get_snapshot(xiaomiao_agent_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now_us = esp_timer_get_time();
    agent_lock();
    const bool within = s_has_committed &&
                        (now_us - s_metrics.committed_us) <= AGENT_VALIDITY_US;
    out_snapshot->state = s_state;
    out_snapshot->has_valid_metrics = within;
    out_snapshot->cpu_valid = within;
    out_snapshot->memory_valid = within;
    out_snapshot->gpu_valid = within && s_metrics.gpu_valid;
    out_snapshot->cpu_temperature_valid = within && s_metrics.cpu_temperature_valid;
    out_snapshot->gpu_temperature_valid = within && s_metrics.gpu_temperature_valid;
    out_snapshot->cpu_percent = s_metrics.cpu_percent;
    out_snapshot->memory_percent = s_metrics.memory_percent;
    out_snapshot->gpu_percent = s_metrics.gpu_percent;
    out_snapshot->cpu_temperature_c = s_metrics.cpu_temperature_c;
    out_snapshot->gpu_temperature_c = s_metrics.gpu_temperature_c;
    out_snapshot->last_success_us = s_has_committed ? s_metrics.committed_us : 0;
    out_snapshot->consecutive_failures = s_consecutive_failures;
    out_snapshot->http_status = s_http_status;
    out_snapshot->last_error = s_last_error;
    agent_unlock();
    return ESP_OK;
}