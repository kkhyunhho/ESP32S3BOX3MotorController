#include "bsp/esp-bsp.h"
#include "motor_cmd.h"
#include "ui.h"
#include "esp_log.h"

#define TAG "MAIN"

void app_main(void)
{
    ESP_LOGI(TAG, "Motor controller starting (bridge mode)");

    bsp_display_start();
    bsp_display_backlight_on();

    motor_cmd_log_banner();

    /* 0 = wait indefinitely (timeout argument, NOT non-blocking). */
    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    ESP_LOGI(TAG, "Ready — touch the display to jog motors");
}
