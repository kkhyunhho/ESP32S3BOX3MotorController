#include "bsp/esp-bsp.h"
#include "motor_cmd.h"
#include "motor_rx.h"
#include "ui.h"
#include "esp_log.h"

#define TAG "MAIN"

void app_main(void)
{
    ESP_LOGI(TAG, "Rail controller starting (USB-serial bridge mode)");

    bsp_display_start();
    bsp_display_backlight_on();

    motor_cmd_log_banner();

    /* 0 = wait indefinitely (timeout argument, NOT non-blocking). */
    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    /* Start after ui_create() so the live-position label exists before
     * the first POS:<mm> line arrives from the host. */
    motor_rx_start();

    ESP_LOGI(TAG, "Ready — touch the display to drive the rail");
}
