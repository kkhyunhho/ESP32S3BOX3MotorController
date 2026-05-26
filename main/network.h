#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_STATE_IDLE,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_DISCONNECTED,
} network_state_t;

/**
 * @brief  Bring up the WiFi STA stack non-blockingly.
 *
 * Initializes NVS, esp_netif, the default event loop and esp_wifi in
 * station mode using CONFIG_MOTORCTRL_WIFI_SSID /
 * CONFIG_MOTORCTRL_WIFI_PASSWORD, then lets esp_wifi handle reconnect
 * on disconnect. Returns immediately — the caller does not wait for
 * association.
 *
 * If CONFIG_MOTORCTRL_WIFI_SSID is empty, this logs a warning and
 * returns ESP_OK without starting WiFi (lets the firmware run in
 * USB-only mode without menuconfig).
 */
esp_err_t       network_init(void);

/**
 * @brief  Return the current WiFi STA connection state.
 *
 * Safe to call from any task; reads a single word updated by the
 * event handler. Useful for status indicators that should distinguish
 * idle, connecting, connected, and disconnected.
 */
network_state_t network_get_state(void);

/**
 * @brief  Convenience predicate: link is up and an IP has been acquired.
 *
 * Equivalent to `network_get_state() == NETWORK_STATE_CONNECTED`.
 * Intended for gating one-shot socket sends where the caller does not
 * care about the precise sub-state.
 */
bool            network_is_connected(void);

/**
 * @brief  Block the caller until the link is up or the timeout expires.
 *
 * @param  timeout_ms  Maximum time to wait. 0 returns immediately.
 *                     UINT32_MAX waits forever.
 */
void            network_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
