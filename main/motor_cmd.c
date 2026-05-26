#include "motor_cmd.h"
#include "cmd_link.h"
#include "esp_log.h"

#include <stdio.h>

#define TAG "MOTOR"

/* Commands are pushed over the TCP link (cmd_link). Drops silently
 * when no client is connected, so the LVGL UI never needs to gate
 * itself on link state. */

void motor_cmd_log_banner(void)
{
    ESP_LOGI(TAG, "Bridge mode: motor commands sent via TCP to PC");
}

void motor_cmd_jog_start(axis_t axis, dir_t dir)
{
    switch (axis) {
    case AXIS_Z:
        cmd_link_send(dir == DIR_POS ? "CMD:Z+\n" : "CMD:Z-\n");
        break;
    case AXIS_X:
        cmd_link_send(dir == DIR_POS ? "CMD:X+\n" : "CMD:X-\n");
        break;
    }
}

void motor_cmd_jog_stop(axis_t axis)
{
    switch (axis) {
    case AXIS_Z:
        cmd_link_send("CMD:Z0\n");
        break;
    case AXIS_X:
        cmd_link_send("CMD:X0\n");
        break;
    }
}

void motor_cmd_home(void)
{
    cmd_link_send("CMD:HOME\n");
}

void motor_cmd_move_to(int x_mm, int z_mm)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "CMD:MOVE X %d Z %d\n", x_mm, z_mm);
    cmd_link_send(buf);
}
