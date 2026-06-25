#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Start the TCP command server on the given port.
 *
 * Spawns a background task that waits for Wi-Fi to associate, then
 * binds INADDR_ANY:<port>, listens, and accepts a single client at
 * a time. Returns immediately — the caller does not block on the
 * link being up.
 *
 * Safe to call right after `network_init()` even if Wi-Fi has not
 * associated yet; the task waits internally.
 *
 * @return ESP_OK on task creation, ESP_FAIL otherwise.
 */
esp_err_t cmd_link_start(uint16_t port);

/**
 * @brief  Push one NUL-terminated command line to the active client.
 *
 * Caller must include the trailing newline (matches the pre-existing
 * `CMD:Z+\n` framing used by bridge.py). No-op (silently drops the
 * line) when no client is currently connected — this lets the LVGL
 * UI fire commands freely without checking link state.
 *
 * Thread-safe; intended to be called from the LVGL UI task.
 */
void cmd_link_send(const char *line);

/* Diagnostics getters — used by the Status UI tab.
 *
 * cmd_link_has_client:   true iff a TCP client is currently connected.
 * cmd_link_get_peer:     copies "ip:port" of the connected client, or
 *                        "" when none. `cap` includes the NUL.
 * cmd_link_get_last_cmd: copies the most recent line that was handed
 *                        to send() (trailing '\n' stripped), or "" if
 *                        nothing has been sent since boot.
 * cmd_link_last_send_us: `esp_timer_get_time()` timestamp of the last
 *                        successful send; 0 when never.
 * cmd_link_get_position: copies the latest "X <mm> Z <mm>" position the
 *                        PC pushed via "POS:...", or "" if none / the
 *                        link is down. `cap` includes the NUL. */
bool    cmd_link_has_client(void);
void    cmd_link_get_peer(char *out, size_t cap);
void    cmd_link_get_last_cmd(char *out, size_t cap);
int64_t cmd_link_last_send_us(void);
void    cmd_link_get_position(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
