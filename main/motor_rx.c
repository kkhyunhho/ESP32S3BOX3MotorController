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
#define STAT_PREFIX   "STAT:"

/* If no STAT frame arrives within this window the bridge is down or the
 * USB link dropped; the Rail tab then shows "Disconnected". */
#define STALE_MS      3000

/* Last position reported by a STAT frame, reused when the link goes
 * stale so the displacement readout holds its last value. */
static float s_last_mm = 0.0f;

/* Parse one received line; act only on "STAT:<conn>:<mm>" frames, where
 * <conn> is 1/0 (rail link health) and <mm> is the rail position =
 * displacement from origin. Anything else (the device's own CMD: echoes,
 * stray log bytes) is ignored. Returns true if a STAT frame was consumed
 * so the caller can refresh the link-liveness timer. */
static bool parse_line(const char *line)
{
    size_t prefix_len = strlen(STAT_PREFIX);
    if (strncmp(line, STAT_PREFIX, prefix_len) != 0) {
        return false;
    }
    const char *body = line + prefix_len;
    const char *sep = strchr(body, ':');
    if (sep == NULL) {
        return false;
    }
    bool connected = (body[0] == '1');
    s_last_mm = strtof(sep + 1, NULL);
    ui_rail_set_status(connected, s_last_mm);
    return true;
}

static void rx_task(void *arg)
{
    (void)arg;
    static char line[RX_LINE_MAX];
    size_t len = 0;
    uint8_t chunk[CHUNK_SIZE];
    TickType_t last_stat = xTaskGetTickCount();
    bool shown_stale = false;

    while (1) {
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk),
                                           pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)chunk[i];
            if (c == '\n' || c == '\r') {
                if (len > 0) {
                    line[len] = '\0';
                    if (parse_line(line)) {
                        last_stat = xTaskGetTickCount();
                        shown_stale = false;
                    }
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
        /* Host-link liveness: no STAT for STALE_MS means the bridge is
         * down or the USB link dropped -> show Disconnected, holding the
         * last known displacement. */
        if (!shown_stale &&
            (xTaskGetTickCount() - last_stat) > pdMS_TO_TICKS(STALE_MS)) {
            ui_rail_set_status(false, s_last_mm);
            shown_stale = true;
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
