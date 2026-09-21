/*
 * Minimal DNS responder implementation (goal node 10).
 *
 * Packet handling follows the ESP-IDF captive-portal example: copy the
 * query, set the response bit and append one A record that points at the
 * SoftAP. Only one question is answered, which covers what phones and
 * captive-portal probes ask for; anything else is answered with an empty
 * NOERROR reply instead of being dropped, so clients do not retry for
 * seconds.
 *
 * Shutdown: lwip does not wake a blocking recvfrom() on shutdown() for a
 * UDP socket (measured on hardware: the task stayed blocked and the port
 * was never released), so the socket carries a receive timeout and the
 * loop re-checks its run flag between timeouts. The socket is closed only
 * by the task that owns it, which removes any shutdown/close race.
 */

#include "xiaomiao_wifi_dns.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char TAG[] = "wifi_dns";

#define DNS_PORT        53
#define DNS_MAX_PACKET  256
#define DNS_TASK_STACK  3072
#define DNS_TASK_PRIO   5
/* How long one receive may block before the task re-checks its flag. */
#define DNS_RECV_TIMEOUT_US 200000
/* How long stop() waits for the task to leave before giving up. */
#define DNS_STOP_WAIT_MS 500

#define DNS_TYPE_A       1
#define DNS_CLASS_IN     1
#define DNS_FLAG_QR      0x8000
#define DNS_FLAG_RD      0x0100
#define DNS_FLAG_RA      0x0080
#define DNS_ANSWER_TTL_S 60

typedef struct __attribute__((__packed__)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

static TaskHandle_t s_task;
static int s_sock = -1;
static volatile bool s_running;
static esp_ip4_addr_t s_answer;

/*
 * Walk one question name and return the offset just past it, or 0 when
 * the packet ends early. Compression pointers are not followed: a query
 * from a client never needs them here.
 */
static size_t dns_skip_name(const uint8_t *packet, size_t length, size_t offset)
{
    while (offset < length) {
        const uint8_t label_len = packet[offset];

        if (label_len == 0) {
            return offset + 1;
        }
        if ((label_len & 0xC0) != 0) {
            /* Compressed or reserved label: refuse rather than guess. */
            return 0;
        }

        offset += (size_t)label_len + 1;
    }

    return 0;
}

/*
 * Build the reply for `request`. Returns the reply length, or 0 when the
 * packet is not a standard single-question query this responder serves.
 */
static size_t dns_build_reply(const uint8_t *request, size_t request_len, uint8_t *reply,
                              size_t reply_capacity)
{
    if (request_len < sizeof(dns_header_t) || request_len > reply_capacity) {
        return 0;
    }

    const dns_header_t *req_header = (const dns_header_t *)request;
    const uint16_t flags = ntohs(req_header->flags);
    const uint16_t qd_count = ntohs(req_header->qd_count);

    /* Standard query only, and exactly one question. */
    if ((flags & 0x7800) != 0 || qd_count != 1) {
        return 0;
    }

    const size_t question_end = dns_skip_name(request, request_len, sizeof(dns_header_t));
    if (question_end == 0 || question_end + 4 > request_len) {
        return 0;
    }

    const uint16_t qtype = (uint16_t)((request[question_end] << 8) | request[question_end + 1]);
    const uint16_t qclass = (uint16_t)((request[question_end + 2] << 8) | request[question_end + 3]);
    const size_t question_len = question_end + 4;

    memcpy(reply, request, question_len);

    dns_header_t *header = (dns_header_t *)reply;
    header->flags = htons((uint16_t)(DNS_FLAG_QR | DNS_FLAG_RA | (flags & DNS_FLAG_RD)));
    header->qd_count = htons(1);

    /* Only IN/A gets an address; every other question is answered with an
     * empty NOERROR so the client moves on immediately. */
    if (qtype != DNS_TYPE_A || qclass != DNS_CLASS_IN) {
        header->an_count = 0;
        header->ns_count = 0;
        header->ar_count = 0;
        return question_len;
    }

    if (question_len + 16 > reply_capacity) {
        return 0;
    }

    uint8_t *answer = reply + question_len;
    /* Name pointer to offset 12 (the question name) plus the fixed A
     * record body. */
    answer[0] = 0xC0;
    answer[1] = 0x0C;
    answer[2] = 0x00;
    answer[3] = (uint8_t)DNS_TYPE_A;
    answer[4] = 0x00;
    answer[5] = (uint8_t)DNS_CLASS_IN;
    answer[6] = 0x00;
    answer[7] = 0x00;
    answer[8] = 0x00;
    answer[9] = (uint8_t)DNS_ANSWER_TTL_S;
    answer[10] = 0x00;
    answer[11] = 0x04;
    memcpy(&answer[12], &s_answer.addr, sizeof(s_answer.addr));

    header->an_count = htons(1);
    header->ns_count = 0;
    header->ar_count = 0;
    return question_len + 16;
}

static void dns_task(void *arg)
{
    (void)arg;

    uint8_t request[DNS_MAX_PACKET];
    uint8_t reply[DNS_MAX_PACKET];

    while (s_running) {
        struct sockaddr_in source;
        socklen_t source_len = sizeof(source);

        const int received = recvfrom(s_sock, request, sizeof(request), 0,
                                      (struct sockaddr *)&source, &source_len);
        if (received < 0) {
            /* A timeout is the normal idle case: go back and re-check the
             * run flag instead of treating it as a failure. */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }
        if (received == 0) {
            /* An empty datagram carries no question. */
            continue;
        }

        const size_t reply_len = dns_build_reply(request, (size_t)received, reply, sizeof(reply));
        if (reply_len == 0) {
            continue;
        }

        (void)sendto(s_sock, reply, reply_len, 0, (struct sockaddr *)&source, source_len);
    }

    /* Owned by this task: closing here keeps ownership and lifetime in
     * one place. */
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t xiaomiao_wifi_dns_start(esp_ip4_addr_t answer)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_answer = answer;

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(DNS_PORT);

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket creation failed: errno %d", errno);
        return ESP_FAIL;
    }

    if (bind(s_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind to port %d failed: errno %d", DNS_PORT, errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    /*
     * The receive timeout is what lets the task notice a stop request:
     * lwip does not interrupt a blocked recvfrom() on shutdown() for UDP.
     * LWIP_SO_RCVTIMEO is on by default in the ESP-IDF lwip port.
     */
    struct timeval recv_timeout;
    recv_timeout.tv_sec = 0;
    recv_timeout.tv_usec = DNS_RECV_TIMEOUT_US;
    if (setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0) {
        ESP_LOGE(TAG, "setting the receive timeout failed: errno %d", errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    s_running = true;
    if (xTaskCreate(dns_task, "wifi_dns", DNS_TASK_STACK, NULL, DNS_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "task creation failed");
        s_running = false;
        close(s_sock);
        s_sock = -1;
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "responder started");
    return ESP_OK;
}

void xiaomiao_wifi_dns_stop(void)
{
    if (s_task == NULL) {
        return;
    }

    /*
     * The task notices the flag within one receive timeout and closes its
     * own socket afterwards, so the socket keeps exactly one owner and no
     * shutdown/close race exists.
     */
    s_running = false;

    for (int waited = 0; waited < DNS_STOP_WAIT_MS && s_task != NULL; waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_task != NULL) {
        ESP_LOGW(TAG, "responder task did not stop in time");
    }
}
