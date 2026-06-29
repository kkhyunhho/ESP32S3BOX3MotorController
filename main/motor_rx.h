#pragma once

#include "esp_err.h"

/* Start the USB-Serial-JTAG reader task. It parses "POS:<mm>\n" status
 * lines sent by the host bridge (rail_bridge.py) and pushes the value
 * into the live-position label on the Rail tab via ui_rail_set_position.
 *
 * Returns ESP_OK on success, or the driver-install error otherwise. */
esp_err_t motor_rx_start(void);
