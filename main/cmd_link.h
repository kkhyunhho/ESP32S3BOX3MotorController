#pragma once

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

#ifdef __cplusplus
}
#endif
