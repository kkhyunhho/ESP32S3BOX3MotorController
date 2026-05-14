#include "motor_cmd.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MOTOR"

/* fflush(stdout) after every printf forces the command out to the USB
 * serial port immediately. Without it, line buffering can delay the
 * byte stream and make jog buttons feel laggy. */

void motor_cmd_log_banner(void)
{
    ESP_LOGI(TAG, "Bridge mode: motor commands sent via USB serial to PC");
}

void motor_cmd_jog_start(axis_t axis, dir_t dir)
{
    switch (axis) {
    case AXIS_Z:
        printf(dir == DIR_POS ? "CMD:Z+\n" : "CMD:Z-\n");
        break;
    case AXIS_X:
        printf(dir == DIR_POS ? "CMD:X+\n" : "CMD:X-\n");
        break;
    }
    fflush(stdout);
}

void motor_cmd_jog_stop(axis_t axis)
{
    switch (axis) {
    case AXIS_Z:
        printf("CMD:Z0\n");
        break;
    case AXIS_X:
        printf("CMD:X0\n");
        break;
    }
    fflush(stdout);
}

void motor_cmd_home(void)
{
    printf("CMD:HOME\n");
    fflush(stdout);
}
