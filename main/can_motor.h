#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* MKS CAN response status bytes */
#define MKS_STATUS_FAIL     0x00
#define MKS_STATUS_OK       0x01
#define MKS_STATUS_COMPLETE 0x02
#define MKS_STATUS_LIMIT    0x03
#define MKS_STATUS_TIMEOUT  0xFF
#define MKS_STATUS_TX_ERR   0xFE

esp_err_t can_motor_init(void);
void can_motor_deinit(void);

/*
 * Send one MKS CAN command and optionally wait for the motor response.
 * Returns: response status byte, MKS_STATUS_TIMEOUT, or MKS_STATUS_TX_ERR.
 * Broadcast/group IDs (can_id == 0x00 or group) do not generate responses;
 * pass wait_resp = false for those.
 */
uint8_t can_motor_cmd(uint32_t can_id, uint8_t cmd,
                      const uint8_t *data, uint8_t data_len,
                      bool wait_resp);

/*
 * Send F6H speed-mode jog command (fire-and-forget, no response wait).
 * speed_rpm == 0 acts as a stop command.
 */
esp_err_t can_motor_jog(uint32_t can_id, bool cw,
                        uint16_t speed_rpm, uint8_t accel);

/* Drain all pending RX frames from the TWAI queue (discard). */
void can_motor_drain_rx(void);
