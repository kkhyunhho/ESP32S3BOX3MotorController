#include "motor_rx.h"

#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ui.h"

#define TAG "MOTOR_RX"

#define RX_BUF_SIZE   256
#define RX_LINE_MAX      32
#define CHUNK_SIZE    64
#define POS_PREFIX    "POS:"

/* Parse one received line; act only on "POS:<mm>" frames. Anything else
 * (the device's own CMD: echoes, stray log bytes) is ignored. */
static void parse_line(const char *line)
{
    size_t prefix_len = strlen(POS_PREFIX);
    if (strncmp(line, POS_PREFIX, prefix_len) != 0) {
        return;
    }
    float mm = strtof(line + prefix_len, NULL);
    ui_rail_set_position(mm);
}

static void rx_task(void *arg)
{
    (void)arg;
    static char line[RX_LINE_MAX];
    size_t len = 0;
    uint8_t chunk[CHUNK_SIZE];

    while (1) {
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk),
                                           pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)chunk[i];
            if (c == '\n' || c == '\r') {
                if (len > 0) {
                    line[len] = '\0';
                    parse_line(line);
                    len = 0;
                }
                continue;
            }
            if (len < RX_LINE_MAX - 1) {
                line[len++] = c;
            } else {
                len = 0;  /* overflow before newline: drop the line */
            }
        }
    }
}

esp_err_t motor_rx_start(void)
{
    /* The console also runs on USB-Serial-JTAG; installing this driver
     * lets the firmware read host->device bytes. Validate on hardware
     * that RX coexists with the console (see the project plan's risk
     * note); if not, fall back to displaying the commanded target. */
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = RX_BUF_SIZE,
        .tx_buffer_size = RX_BUF_SIZE,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    xTaskCreatePinnedToCore(rx_task, "motor_rx", 4096, NULL, 4, NULL, 1);
    return ESP_OK;
}
