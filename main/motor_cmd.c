#include "motor_cmd.h"

#include <stdio.h>

#include "esp_log.h"

#define TAG "MOTOR"

/* Commands are written to stdout, which on the ESP32-S3-BOX-3 is the
 * USB-Serial-JTAG CDC port. The host bridge (rail_bridge.py) reads the
 * CMD: lines and ignores interleaved ESP-IDF log output. fflush forces
 * each line out immediately instead of waiting on the line buffer. */

static void send_line(const char *line)
{
    fputs(line, stdout);
    fflush(stdout);
}

void motor_cmd_log_banner(void)
{
    ESP_LOGI(TAG, "USB-serial bridge mode: motor commands on stdout");
}

void motor_cmd_jog_start(axis_t axis, dir_t dir)
{
    switch (axis) {
    case AXIS_Z:
        send_line(dir == DIR_POS ? "CMD:Z+\n" : "CMD:Z-\n");
        break;
    case AXIS_X:
        send_line(dir == DIR_POS ? "CMD:X+\n" : "CMD:X-\n");
        break;
    case AXIS_Y:
        send_line(dir == DIR_POS ? "CMD:Y+\n" : "CMD:Y-\n");
        break;
    }
}

void motor_cmd_jog_stop(axis_t axis)
{
    switch (axis) {
    case AXIS_Z:
        send_line("CMD:Z0\n");
        break;
    case AXIS_X:
        send_line("CMD:X0\n");
        break;
    case AXIS_Y:
        send_line("CMD:Y0\n");
        break;
    }
}

void motor_cmd_home(void)
{
    send_line("CMD:HOME\n");
}

void motor_cmd_move_to(int x_mm, int z_mm)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "CMD:MOVE X %d Z %d\n", x_mm, z_mm);
    send_line(buf);
}
