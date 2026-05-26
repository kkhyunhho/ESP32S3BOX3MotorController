#include "bsp/esp-bsp.h"
#include "cmd_link.h"
#include "motor_cmd.h"
#include "network.h"
#include "ui.h"
#include "esp_log.h"

#define TAG "MAIN"

/* TCP port the cmd_link server listens on. Mirror this in bridge.py's
 * ESP32_PORT constant. */
#define CMD_LINK_PORT  3333

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

    /* network_init() is non-blocking; cmd_link_start spawns a task
     * that waits internally for the link to come up before listening,
     * so we can chain them without a sync point. */
    network_init();
    cmd_link_start(CMD_LINK_PORT);

    ESP_LOGI(TAG, "Ready — touch the display to jog motors");
}
