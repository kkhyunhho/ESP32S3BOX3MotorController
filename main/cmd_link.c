#include "cmd_link.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "network.h"

static const char *TAG = "cmd_link";

#define ACCEPT_BACKLOG          1
#define KEEPALIVE_IDLE_S        7
#define KEEPALIVE_INTERVAL_S    3
#define KEEPALIVE_COUNT         3

/* -1 = no client. Protected by s_mutex for assignment / read; actual
 * recv()/send() syscalls happen outside the mutex so the UI task is
 * never blocked waiting for the server task's recv() to return. */
static int s_client_fd = -1;
static SemaphoreHandle_t s_mutex;
static uint16_t s_port;

/* Diagnostics state — all protected by s_mutex. Sized to comfortably
 * hold one peer endpoint string and one CMD:* line. */
static char s_peer[24];           /* "xxx.xxx.xxx.xxx:ppppp" + NUL */
static char s_last_cmd[24];       /* longest is "CMD:HOME" — keep slack */
static int64_t s_last_send_us;    /* 0 means "no send yet since boot" */
static char s_pos[32];            /* latest "X .. Z .." from PC; "" = none */

void cmd_link_send(const char *line)
{
    if (line == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int fd = s_client_fd;
    xSemaphoreGive(s_mutex);
    if (fd < 0) {
        return;
    }

    size_t len = strlen(line);
    ssize_t n = send(fd, line, len, MSG_DONTWAIT);
    if (n < 0) {
        ESP_LOGW(TAG, "send failed (errno=%d), dropping client", errno);
        /* shutdown wakes the recv() in server_task; that task does the
         * actual close so the fd is freed in exactly one place. */
        shutdown(fd, SHUT_RDWR);
        return;
    }

    /* Stash the command + timestamp for the diagnostics UI. Drop the
     * trailing newline so the label reads "CMD:Z+" instead of forcing
     * a layout break. */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t copy_len = len;
    if (copy_len > 0 && line[copy_len - 1] == '\n') {
        copy_len--;
    }
    if (copy_len >= sizeof(s_last_cmd)) {
        copy_len = sizeof(s_last_cmd) - 1;
    }
    memcpy(s_last_cmd, line, copy_len);
    s_last_cmd[copy_len] = '\0';
    s_last_send_us = esp_timer_get_time();
    xSemaphoreGive(s_mutex);
}

/* Handle one newline-delimited line received from the PC. The only line
 * we consume is the position feedback "POS:X <mm> Z <mm>"; the rest is
 * ignored. Stores just the "X .. Z .." payload for the UI to display. */
static void handle_rx_line(const char *line)
{
    if (strncmp(line, "POS:", 4) != 0) {
        return;
    }
    const char *payload = line + 4;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(s_pos, payload, sizeof(s_pos) - 1);
    s_pos[sizeof(s_pos) - 1] = '\0';
    xSemaphoreGive(s_mutex);
}

static void server_task(void *arg)
{
    (void)arg;

    /* Bind/listen only after the link is up so accept() does not spin
     * on a netif that has not been configured yet. */
    network_wait_connected(UINT32_MAX);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        ESP_LOGE(TAG, "socket() failed (errno=%d)", errno);
        vTaskDelete(NULL);
        return;
    }

    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(s_port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed (errno=%d)", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    if (listen(listen_fd, ACCEPT_BACKLOG) < 0) {
        ESP_LOGE(TAG, "listen() failed (errno=%d)", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "listening on :%u", s_port);

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int client = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
        if (client < 0) {
            ESP_LOGW(TAG, "accept() failed (errno=%d)", errno);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* TCP keepalive so a wedged bridge.py is detected within ~16 s
         * (7 + 3 * 3) instead of looking like a forever-quiet peer. */
        int ka = 1;
        int idle = KEEPALIVE_IDLE_S;
        int intv = KEEPALIVE_INTERVAL_S;
        int cnt  = KEEPALIVE_COUNT;
        setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
        setsockopt(client, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(client, IPPROTO_TCP, TCP_KEEPINTVL, &intv, sizeof(intv));
        setsockopt(client, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));

        char ip[16];
        inet_ntoa_r(caddr.sin_addr, ip, sizeof(ip));
        ESP_LOGI(TAG, "client connected: %s:%u",
                 ip, ntohs(caddr.sin_port));

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_client_fd = client;
        snprintf(s_peer, sizeof(s_peer), "%s:%u",
                 ip, ntohs(caddr.sin_port));
        xSemaphoreGive(s_mutex);

        /* Block until the client closes or cmd_link_send shuts the fd
         * down after a write error. The PC pushes "POS:..." position
         * updates back to us, so accumulate bytes into newline-delimited
         * lines and hand each one to handle_rx_line. */
        char rx[64];
        char line[64];
        size_t line_len = 0;
        int n;
        while ((n = recv(client, rx, sizeof(rx), 0)) > 0) {
            for (int i = 0; i < n; i++) {
                char c = rx[i];
                if (c == '\n' || c == '\r') {
                    if (line_len > 0) {
                        line[line_len] = '\0';
                        handle_rx_line(line);
                        line_len = 0;
                    }
                } else if (line_len < sizeof(line) - 1) {
                    line[line_len++] = c;
                } else {
                    line_len = 0;  /* oversized line — drop it */
                }
            }
        }

        ESP_LOGI(TAG, "client disconnected");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (s_client_fd == client) {
            s_client_fd = -1;
            s_peer[0] = '\0';
            s_pos[0] = '\0';  /* position is stale once the PC is gone */
        }
        xSemaphoreGive(s_mutex);
        close(client);
    }
}

bool cmd_link_has_client(void)
{
    if (s_mutex == NULL) {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool connected = (s_client_fd >= 0);
    xSemaphoreGive(s_mutex);
    return connected;
}

void cmd_link_get_peer(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(out, s_peer, cap - 1);
    out[cap - 1] = '\0';
    xSemaphoreGive(s_mutex);
}

void cmd_link_get_last_cmd(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(out, s_last_cmd, cap - 1);
    out[cap - 1] = '\0';
    xSemaphoreGive(s_mutex);
}

void cmd_link_get_position(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strncpy(out, s_pos, cap - 1);
    out[cap - 1] = '\0';
    xSemaphoreGive(s_mutex);
}

int64_t cmd_link_last_send_us(void)
{
    if (s_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int64_t ts = s_last_send_us;
    xSemaphoreGive(s_mutex);
    return ts;
}

esp_err_t cmd_link_start(uint16_t port)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    s_port = port;
    BaseType_t ok = xTaskCreate(server_task, "cmd_link", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
