/*
 * Agent Service implementation (goal node 11).
 *
 * Responsibilities, in the order they appear below:
 *   1. LAN service discovery - UDP broadcast request and dynamic URL
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
 * Two Workers share the one discovered Agent link (host + HTTP port):
 * the metrics Worker owns discovery and the 1-second metrics poll, the
 * quota Worker runs its own slower schedule for both providers. They
 * never block each other, and each provider keeps an independent state
 * and snapshot (AI quota pages goal, decision 7).
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

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "xiaomiao_wifi_service.h"

static const char TAG[] = "agent_svc";

/* `http://` + longest IPv4 + `:port` + the longest path + terminator. */
#define AGENT_URL_MAX 64
/* Fixed LAN discovery endpoint shared with pc-agent/monitor.py. */
#define AGENT_DISCOVERY_PORT 8767
#define AGENT_DISCOVERY_TIMEOUT_US (1500 * 1000)
#define AGENT_DISCOVERY_REQUEST "XIAOMIAO_AGENT_DISCOVER_V1"
#define AGENT_DISCOVERY_RESPONSE_PREFIX "XMA1"
#define AGENT_DISCOVERY_RESPONSE_SIZE 6
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

/* Quota responses hold two short windows, so 1 KB is a generous ceiling
 * and anything longer is rejected whole (AI quota pages goal, CP2). */
#define AGENT_QUOTA_RESPONSE_MAX 1024
/* Quota freshness: a successful round older than this reads as STALE.
 * Deliberately separate from the 3-second PC metrics rule. */
#define AGENT_QUOTA_STALE_US (180 * 1000 * 1000)
/* Low-frequency plan: 30 s while a provider is healthy, 10 s while it
 * is degraded, so recovery is noticed without hammering the Agent. */
#define AGENT_QUOTA_POLL_ONLINE_MS 30000
#define AGENT_QUOTA_POLL_RETRY_MS 10000
#define AGENT_QUOTA_TASK_STACK_BYTES 4096
#define AGENT_QUOTA_TASK_PRIORITY 3
/* The console ``MM-DD HH:MM`` text is 11 characters; longer or shorter
 * text from the Agent is treated as unknown instead of being rendered. */
#define AGENT_QUOTA_TIME_CHARS 11
/* Upper bound of a plausible countdown (~400 days); anything larger is
 * invalid data, not a value to render. */
#define AGENT_QUOTA_MAX_RESET_IN_SEC (400LL * 24 * 60 * 60)
#define AGENT_METRICS_PATH "/api/v1/pc/metrics"

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

/*
 * One committed quota window. Labels and validity flags follow the
 * public xiaomiao_quota_window_t; this is the cached copy the Service
 * keeps after a successful round.
 */
typedef struct {
    bool present;
    bool remaining_valid;
    uint8_t remaining_percent;
    bool reset_time_valid;
    char reset_at_local[XIAOMIAO_QUOTA_TIME_MAX];
    bool reset_countdown_valid;
    int64_t reset_in_sec;
} agent_quota_window_t;

/*
 * One provider's quota cache. The window values survive failed rounds
 * so the UI can keep showing them under STALE/OFF; only ``state``,
 * ``http_status`` and ``last_error`` follow the latest round.
 */
typedef struct {
    bool has_snapshot;
    int64_t committed_us;
    agent_quota_window_t windows[XIAOMIAO_QUOTA_WINDOWS];
    xiaomiao_quota_state_t state;
    int http_status;
    esp_err_t last_error;
} agent_quota_t;

/* Canonical window labels and routes per provider; the JSON is matched
 * against these instead of trusting any label or array order. */
static const char *const AGENT_QUOTA_LABELS[XIAOMIAO_QUOTA_PROVIDER_COUNT][XIAOMIAO_QUOTA_WINDOWS] = {
    {"5H", "7D"},
    {"5H", "1W"},
};
static const char *const AGENT_QUOTA_IDS[XIAOMIAO_QUOTA_PROVIDER_COUNT] = {"codex", "zhipu"};
static const char *const AGENT_QUOTA_PATHS[XIAOMIAO_QUOTA_PROVIDER_COUNT] = {
    "/api/v1/quotas/codex",
    "/api/v1/quotas/zhipu",
};
/* Longest dotted-quad plus terminator. */
#define AGENT_HOST_MAX 16

static SemaphoreHandle_t s_lock;

static bool s_initialized;
static esp_err_t s_init_result = ESP_OK;

/* Protected by s_lock: the single discovered Agent link, written by the
 * metrics Worker when discovery succeeds and cleared whenever Wi-Fi or
 * the HTTP endpoint fails. The quota Worker only reads it. */
static char s_agent_host[AGENT_HOST_MAX];
static uint16_t s_agent_port;
static bool s_agent_link_ready;

/* Protected by s_lock. */
static xiaomiao_agent_state_t s_state = XIAOMIAO_AGENT_UNINITIALIZED;
static agent_metrics_t s_metrics;
static bool s_has_committed;
static uint32_t s_consecutive_failures;
static int s_http_status;
static esp_err_t s_last_error = ESP_OK;
static agent_quota_t s_quotas[XIAOMIAO_QUOTA_PROVIDER_COUNT];

/* ------------------------------------------------------------------ */
/* 1. LAN service discovery                                             */
/* ------------------------------------------------------------------ */

/*
 * Build one request URL from a discovered Agent address. The buffer is
 * caller-owned, so a local copy can never be overwritten mid-request by
 * the other Worker.
 */
static bool agent_build_url(const char *host, uint16_t port, const char *path,
                            char *out_url, size_t capacity)
{
    const int written = snprintf(out_url, capacity, "http://%s:%u%s",
                                 host, (unsigned int)port, path);
    return written > 0 && written < (int)capacity;
}

/*
 * Discover one Agent on the current Wi-Fi LAN. The PC replies directly
 * to this socket, so the datagram source address is the current PC IPv4;
 * the six-byte payload advertises its HTTP port.
 */
static esp_err_t agent_discover(char *host_out, size_t host_capacity, uint16_t *port_out)
{
    if (host_out == NULL || port_out == NULL || host_capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    host_out[0] = '\0';
    *port_out = 0;

    const int discovery_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (discovery_socket < 0) {
        ESP_LOGW(TAG, "discovery socket failed: errno=%d", errno);
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    const int broadcast_enabled = 1;
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enabled, sizeof(broadcast_enabled)) != 0) {
        ESP_LOGW(TAG, "enable discovery broadcast failed: errno=%d", errno);
        goto cleanup;
    }

    const struct sockaddr_in broadcast_address = {
        .sin_family = AF_INET,
        .sin_port = htons(AGENT_DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_BROADCAST),
    };
    const char request[] = AGENT_DISCOVERY_REQUEST;
    const ssize_t sent = sendto(discovery_socket, request, sizeof(request) - 1, 0,
                                (const struct sockaddr *)&broadcast_address,
                                sizeof(broadcast_address));
    if (sent != (ssize_t)(sizeof(request) - 1)) {
        ESP_LOGD(TAG, "send discovery request failed: errno=%d", errno);
        goto cleanup;
    }

    const int64_t deadline_us = esp_timer_get_time() + AGENT_DISCOVERY_TIMEOUT_US;
    for (;;) {
        const int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            result = ESP_ERR_NOT_FOUND;
            break;
        }

        const struct timeval timeout = {
            .tv_sec = (long)(remaining_us / 1000000),
            .tv_usec = (long)(remaining_us % 1000000),
        };
        if (setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout)) != 0) {
            ESP_LOGW(TAG, "set discovery timeout failed: errno=%d", errno);
            break;
        }

        uint8_t response[AGENT_DISCOVERY_RESPONSE_SIZE + 1];
        struct sockaddr_in source_address = {0};
        socklen_t source_length = sizeof(source_address);
        const ssize_t received = recvfrom(discovery_socket, response,
                                          sizeof(response), 0,
                                          (struct sockaddr *)&source_address,
                                          &source_length);
        if (received < 0) {
            result = (errno == EAGAIN || errno == EWOULDBLOCK)
                         ? ESP_ERR_NOT_FOUND
                         : ESP_FAIL;
            break;
        }
        if (received != AGENT_DISCOVERY_RESPONSE_SIZE ||
            memcmp(response, AGENT_DISCOVERY_RESPONSE_PREFIX, 4) != 0 ||
            source_address.sin_family != AF_INET ||
            source_address.sin_addr.s_addr == htonl(INADDR_ANY)) {
            ESP_LOGD(TAG, "ignoring invalid discovery response");
            continue;
        }

        const uint16_t http_port = (uint16_t)(((uint16_t)response[4] << 8) |
                                              (uint16_t)response[5]);
        if (http_port == 0) {
            ESP_LOGD(TAG, "ignoring discovery response with port 0");
            continue;
        }

        const uint32_t host = ntohl(source_address.sin_addr.s_addr);
        const int host_length = snprintf(host_out, host_capacity,
                                         "%u.%u.%u.%u",
                                         (unsigned int)((host >> 24) & 0xff),
                                         (unsigned int)((host >> 16) & 0xff),
                                         (unsigned int)((host >> 8) & 0xff),
                                         (unsigned int)(host & 0xff));
        if (host_length <= 0 || host_length >= (int)host_capacity) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        *port_out = http_port;
        ESP_LOGI(TAG, "discovered agent at %s:%u", host_out,
                 (unsigned int)http_port);
        result = ESP_OK;
        break;
    }

cleanup:
    close(discovery_socket);
    return result;
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
static bool agent_fetch(const char *url, char *body, size_t capacity,
                        size_t *out_len, int *out_status)
{
    bool ok = false;
    *out_status = 0;
    *out_len = 0;

    const esp_http_client_config_t config = {
        .url = url,
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
/* 3b. Quota JSON validation                                           */
/* ------------------------------------------------------------------ */

/*
 * Map a label onto the provider's canonical window slot. Only the two
 * target labels are accepted; an unknown label is dropped instead of
 * being placed by array order (AI quota pages goal, decision 2).
 */
static bool agent_quota_label_index(uint8_t provider, const char *label, uint8_t *out_index)
{
    if (label == NULL) {
        return false;
    }
    for (uint8_t index = 0; index < XIAOMIAO_QUOTA_WINDOWS; ++index) {
        if (strcmp(label, AGENT_QUOTA_LABELS[provider][index]) == 0) {
            *out_index = index;
            return true;
        }
    }
    return false;
}

/*
 * Accept only the exact ``MM-DD HH:MM`` shape with plausible digits, so
 * a broken or hostile Agent response cannot push arbitrary text to the
 * screen; anything else leaves the reset time unknown.
 */
static bool agent_quota_time_text(const cJSON *item, char *out_text, size_t capacity)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    const size_t length = strlen(item->valuestring);
    if (length != (size_t)AGENT_QUOTA_TIME_CHARS || length >= capacity) {
        return false;
    }
    const char *text = item->valuestring;
    for (size_t index = 0; index < length; ++index) {
        const char character = text[index];
        if (index == 2) {
            if (character != '-') {
                return false;
            }
            continue;
        }
        if (index == 5) {
            if (character != ' ') {
                return false;
            }
            continue;
        }
        if (index == 8) {
            if (character != ':') {
                return false;
            }
            continue;
        }
        if (character < '0' || character > '9') {
            return false;
        }
    }
    const int hour = (text[6] - '0') * 10 + (text[7] - '0');
    const int minute = (text[9] - '0') * 10 + (text[10] - '0');
    if (hour > 23 || minute > 59) {
        return false;
    }
    memcpy(out_text, text, length + 1);
    return true;
}

/*
 * Validate one quota response. Returns the state to record; only OK
 * fills ``out_windows``. Statuses come from the frozen API v1 contract,
 * an unexpected provider ID or a response without a single target window
 * is invalid data, and everything is bounded before it is copied.
 */
static xiaomiao_quota_state_t agent_parse_quota(const char *body, uint8_t provider,
                                                agent_quota_window_t *out_windows)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return XIAOMIAO_QUOTA_INVALID_DATA;
    }

    xiaomiao_quota_state_t result = XIAOMIAO_QUOTA_INVALID_DATA;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    do {
        if (!cJSON_IsNumber(schema) || schema->valuedouble != 1.0) {
            break;
        }
        if (!cJSON_IsString(status) || status->valuestring == NULL) {
            break;
        }
        if (!cJSON_IsObject(data)) {
            break;
        }
        const cJSON *provider_id = cJSON_GetObjectItemCaseSensitive(data, "provider_id");
        if (!cJSON_IsString(provider_id) || provider_id->valuestring == NULL ||
            strcmp(provider_id->valuestring, AGENT_QUOTA_IDS[provider]) != 0) {
            break;
        }

        if (strcmp(status->valuestring, "auth_required") == 0) {
            result = XIAOMIAO_QUOTA_AUTH_REQUIRED;
            break;
        }
        if (strcmp(status->valuestring, "unavailable") == 0) {
            result = XIAOMIAO_QUOTA_UNAVAILABLE;
            break;
        }
        if (strcmp(status->valuestring, "stale") == 0) {
            result = XIAOMIAO_QUOTA_STALE;
            break;
        }
        if (strcmp(status->valuestring, "invalid_data") == 0) {
            result = XIAOMIAO_QUOTA_INVALID_DATA;
            break;
        }
        if (strcmp(status->valuestring, "ok") != 0) {
            break;
        }

        const cJSON *windows = cJSON_GetObjectItemCaseSensitive(data, "windows");
        if (!cJSON_IsArray(windows) || cJSON_GetArraySize(windows) > XIAOMIAO_QUOTA_WINDOWS) {
            break;
        }

        uint8_t matched = 0;
        const cJSON *window = NULL;
        cJSON_ArrayForEach(window, windows) {
            if (!cJSON_IsObject(window)) {
                continue;
            }
            const cJSON *label = cJSON_GetObjectItemCaseSensitive(window, "label");
            uint8_t index = 0;
            if (!agent_quota_label_index(provider,
                                         cJSON_IsString(label) ? label->valuestring : NULL,
                                         &index)) {
                continue;
            }
            if (out_windows[index].present) {
                continue; /* first occurrence of a label wins */
            }

            agent_quota_window_t parsed;
            memset(&parsed, 0, sizeof(parsed));
            parsed.present = true;

            const cJSON *remaining = cJSON_GetObjectItemCaseSensitive(window, "remaining_percent");
            if (cJSON_IsNumber(remaining)) {
                const double value = remaining->valuedouble;
                if (isfinite((float)value) && value >= 0.0 && value <= 100.0) {
                    parsed.remaining_valid = true;
                    /* The range check above guarantees a non-negative
                     * value, so a bias-and-truncate rounds it without
                     * pulling in libm rounding. */
                    parsed.remaining_percent = (uint8_t)(value + 0.5);
                }
            }

            parsed.reset_time_valid = agent_quota_time_text(
                cJSON_GetObjectItemCaseSensitive(window, "reset_at_local"),
                parsed.reset_at_local, sizeof(parsed.reset_at_local));

            const cJSON *countdown = cJSON_GetObjectItemCaseSensitive(window, "reset_in_sec");
            if (cJSON_IsNumber(countdown)) {
                const double value = countdown->valuedouble;
                if (isfinite((float)value) && value >= 0.0 &&
                    value <= (double)AGENT_QUOTA_MAX_RESET_IN_SEC) {
                    parsed.reset_countdown_valid = true;
                    parsed.reset_in_sec = (int64_t)value;
                }
            }

            out_windows[index] = parsed;
            ++matched;
        }
        if (matched == 0) {
            break;
        }
        result = XIAOMIAO_QUOTA_OK;
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

/*
 * The discovered Agent link is published once by the metrics Worker and
 * read by the quota Worker. Both go through the lock so a request never
 * starts against a host that was just cleared.
 */
static void agent_store_link(const char *host, uint16_t port)
{
    agent_lock();
    snprintf(s_agent_host, sizeof(s_agent_host), "%s", host);
    s_agent_port = port;
    s_agent_link_ready = true;
    agent_unlock();
}

static void agent_clear_link(void)
{
    agent_lock();
    s_agent_host[0] = '\0';
    s_agent_port = 0;
    s_agent_link_ready = false;
    agent_unlock();
}

static bool agent_link_copy(char *host_out, size_t host_capacity, uint16_t *port_out)
{
    agent_lock();
    const bool ready = s_agent_link_ready;
    if (ready) {
        snprintf(host_out, host_capacity, "%s", s_agent_host);
        *port_out = s_agent_port;
    }
    agent_unlock();
    return ready;
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
/* 4b. Quota state                                                     */
/* ------------------------------------------------------------------ */

/*
 * Record a failed or non-ok round. The cached windows and their commit
 * time stay untouched, so the UI keeps the last known values and the
 * state tells it how much to trust them (goal decision 4).
 */
static void agent_quota_note_result(uint8_t provider, int http_status, esp_err_t error,
                                    xiaomiao_quota_state_t state)
{
    agent_lock();
    agent_quota_t *quota = &s_quotas[provider];
    quota->state = state;
    quota->http_status = http_status;
    quota->last_error = error;
    agent_unlock();
}

static void agent_quota_commit(uint8_t provider, const agent_quota_window_t *windows,
                               int http_status)
{
    agent_lock();
    agent_quota_t *quota = &s_quotas[provider];
    memcpy(quota->windows, windows, sizeof(quota->windows));
    quota->has_snapshot = true;
    quota->committed_us = esp_timer_get_time();
    quota->state = XIAOMIAO_QUOTA_OK;
    quota->http_status = http_status;
    quota->last_error = ESP_OK;
    agent_unlock();
}

/* Called when the shared Agent link is gone: both providers read OFF
 * but keep their last values (goal failure paths). */
static void agent_quota_mark_link_down(void)
{
    agent_lock();
    for (uint8_t provider = 0; provider < XIAOMIAO_QUOTA_PROVIDER_COUNT; ++provider) {
        s_quotas[provider].state = XIAOMIAO_QUOTA_OFFLINE;
    }
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

/*
 * Metrics Worker: owns discovery and the 1-second PC metrics poll. It is
 * the only writer of the shared Agent link, so the quota Worker always
 * follows the same host and port.
 */
static void agent_worker_task(void *argument)
{
    (void)argument;
    static char body[AGENT_RESPONSE_MAX];
    char url[AGENT_URL_MAX];
    bool agent_discovered = false;

    for (;;) {
        if (!agent_wifi_is_online()) {
            /* No IPv4 means no route to the Agent; the 3-second rule in
             * get_snapshot() retires the display on its own, so nothing
             * else to clear here (goal node 11, failure paths). */
            agent_discovered = false;
            agent_clear_link();
            agent_set_state(XIAOMIAO_AGENT_WIFI_OFFLINE);
            vTaskDelay(pdMS_TO_TICKS(AGENT_POLL_OFFLINE_MS));
            continue;
        }

        if (!agent_discovered) {
            agent_set_state(XIAOMIAO_AGENT_DISCOVERING);
            char host[AGENT_HOST_MAX];
            uint16_t port = 0;
            const esp_err_t discovery_result = agent_discover(host, sizeof(host), &port);
            if (discovery_result != ESP_OK) {
                agent_note_failure(0, discovery_result);
                agent_set_state(XIAOMIAO_AGENT_RETRY_WAIT);
                agent_clear_link();
                vTaskDelay(pdMS_TO_TICKS(AGENT_RETRY_WAIT_MS));
                continue;
            }
            if (!agent_build_url(host, port, AGENT_METRICS_PATH, url, sizeof(url))) {
                agent_note_failure(0, ESP_ERR_INVALID_SIZE);
                agent_set_state(XIAOMIAO_AGENT_RETRY_WAIT);
                agent_clear_link();
                vTaskDelay(pdMS_TO_TICKS(AGENT_RETRY_WAIT_MS));
                continue;
            }
            agent_store_link(host, port);
            agent_discovered = true;
        }

        agent_set_state(XIAOMIAO_AGENT_CONNECTING);

        size_t body_len = 0;
        int http_status = 0;
        const bool fetched = agent_fetch(url, body, sizeof(body), &body_len, &http_status);

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
            agent_discovered = false;
            agent_clear_link();
        }

        if (success) {
            vTaskDelay(pdMS_TO_TICKS(AGENT_POLL_ONLINE_MS));
        } else {
            agent_set_state(XIAOMIAO_AGENT_RETRY_WAIT);
            vTaskDelay(pdMS_TO_TICKS(AGENT_RETRY_WAIT_MS));
        }
    }
}

/*
 * Quota Worker: independent low-frequency schedule for both providers.
 * It reads the link published by the metrics Worker, requests the two
 * quota routes in turn and never touches the metrics snapshot, its
 * success time or the shared link.
 */
static void agent_quota_worker_task(void *argument)
{
    (void)argument;
    static char body[AGENT_QUOTA_RESPONSE_MAX];
    char host[AGENT_HOST_MAX];

    for (;;) {
        uint16_t port = 0;
        if (!agent_link_copy(host, sizeof(host), &port)) {
            agent_quota_mark_link_down();
            vTaskDelay(pdMS_TO_TICKS(AGENT_QUOTA_POLL_RETRY_MS));
            continue;
        }

        bool all_ok = true;
        for (uint8_t provider = 0; provider < XIAOMIAO_QUOTA_PROVIDER_COUNT; ++provider) {
            char url[AGENT_URL_MAX];
            if (!agent_build_url(host, port, AGENT_QUOTA_PATHS[provider], url, sizeof(url))) {
                agent_quota_note_result(provider, 0, ESP_ERR_INVALID_SIZE,
                                        XIAOMIAO_QUOTA_OFFLINE);
                all_ok = false;
                continue;
            }

            size_t body_len = 0;
            int http_status = 0;
            if (!agent_fetch(url, body, sizeof(body), &body_len, &http_status)) {
                /* No completed HTTP 200: either the Agent is gone (OFF)
                 * or an older Agent build lacks the route (SRC). Neither
                 * case involves invalid JSON. */
                agent_quota_note_result(
                    provider, http_status,
                    http_status == 0 ? ESP_FAIL : ESP_ERR_NOT_SUPPORTED,
                    http_status == 0 ? XIAOMIAO_QUOTA_OFFLINE : XIAOMIAO_QUOTA_UNAVAILABLE);
                all_ok = false;
                continue;
            }

            agent_quota_window_t windows[XIAOMIAO_QUOTA_WINDOWS];
            memset(windows, 0, sizeof(windows));
            const xiaomiao_quota_state_t parsed = agent_parse_quota(body, provider, windows);
            if (parsed == XIAOMIAO_QUOTA_OK) {
                agent_quota_commit(provider, windows, http_status);
                ESP_LOGD(TAG, "quota %s updated", AGENT_QUOTA_IDS[provider]);
            } else {
                agent_quota_note_result(provider, http_status, ESP_ERR_INVALID_RESPONSE, parsed);
                all_ok = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(all_ok ? AGENT_QUOTA_POLL_ONLINE_MS
                                        : AGENT_QUOTA_POLL_RETRY_MS));
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

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        s_state = XIAOMIAO_AGENT_ERROR;
        s_last_error = ESP_ERR_NO_MEM;
        s_init_result = ESP_ERR_NO_MEM;
        return s_init_result;
    }

    s_state = XIAOMIAO_AGENT_WIFI_OFFLINE;
    if (xTaskCreate(agent_worker_task, "agent_svc", AGENT_TASK_STACK_BYTES,
                    NULL, AGENT_TASK_PRIORITY, NULL) != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_state = XIAOMIAO_AGENT_ERROR;
        s_last_error = ESP_ERR_NO_MEM;
        s_init_result = ESP_ERR_NO_MEM;
        return s_init_result;
    }

    /* The quota Worker is a separate low-frequency task, so a quota round
     * or its timeout can never delay the 1-second metrics poll. If it
     * cannot be created, only the quota routes stay OFF while boot and
     * the PC metrics keep working (AI quota pages goal, CP2). */
    if (xTaskCreate(agent_quota_worker_task, "agent_quota", AGENT_QUOTA_TASK_STACK_BYTES,
                    NULL, AGENT_QUOTA_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGW(TAG, "quota worker not started, quota routes stay offline");
    }

    ESP_LOGI(TAG, "agent service ready, waiting for Wi-Fi discovery");
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

esp_err_t xiaomiao_agent_get_quota_snapshot(xiaomiao_quota_provider_t provider,
                                            xiaomiao_quota_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL ||
        (unsigned int)provider >= (unsigned int)XIAOMIAO_QUOTA_PROVIDER_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now_us = esp_timer_get_time();
    memset(out_snapshot, 0, sizeof(*out_snapshot));

    agent_lock();
    const agent_quota_t *quota = &s_quotas[provider];
    xiaomiao_quota_state_t state = quota->state;
    const bool within = quota->has_snapshot &&
                        (now_us - quota->committed_us) <= AGENT_QUOTA_STALE_US;
    if (!s_agent_link_ready) {
        /* No discovered Agent (or no Wi-Fi): OFF, old values kept. */
        state = XIAOMIAO_QUOTA_OFFLINE;
    } else if (quota->has_snapshot && !within &&
               (state == XIAOMIAO_QUOTA_OK || state == XIAOMIAO_QUOTA_UNAVAILABLE ||
                state == XIAOMIAO_QUOTA_INVALID_DATA)) {
        /* The 180-second quota freshness rule, independent of the
         * 3-second PC metrics rule. AUTH_REQUIRED keeps its own meaning
         * and keeps showing LOGIN with the last known values. */
        state = XIAOMIAO_QUOTA_STALE;
    }

    out_snapshot->state = state;
    out_snapshot->has_snapshot = quota->has_snapshot;
    out_snapshot->window_count = XIAOMIAO_QUOTA_WINDOWS;
    out_snapshot->received_us = quota->has_snapshot ? quota->committed_us : 0;
    out_snapshot->http_status = quota->http_status;
    out_snapshot->last_error = quota->last_error;
    for (uint8_t index = 0; index < XIAOMIAO_QUOTA_WINDOWS; ++index) {
        const agent_quota_window_t *source = &quota->windows[index];
        xiaomiao_quota_window_t *target = &out_snapshot->windows[index];
        snprintf(target->label, sizeof(target->label), "%s",
                 AGENT_QUOTA_LABELS[provider][index]);
        target->present = source->present;
        target->remaining_valid = source->remaining_valid;
        target->remaining_percent = source->remaining_percent;
        target->reset_time_valid = source->reset_time_valid;
        memcpy(target->reset_at_local, source->reset_at_local,
               sizeof(target->reset_at_local));
        target->reset_countdown_valid = source->reset_countdown_valid;
        target->reset_in_sec = source->reset_in_sec;
    }
    agent_unlock();
    return ESP_OK;
}
