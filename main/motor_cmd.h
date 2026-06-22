#pragma once

typedef enum { AXIS_X, AXIS_Y, AXIS_Z } axis_t;
typedef enum { DIR_POS, DIR_NEG } dir_t;

void motor_cmd_log_banner(void);

void motor_cmd_jog_start(axis_t axis, dir_t dir);

void motor_cmd_jog_stop(axis_t axis);

void motor_cmd_home(void);

/* Absolute coordinate move on the X/Z plane. Emits
 * "CMD:MOVE X <mm> Z <mm>\n". X/Z are reserved for the future ball-screw
 * motors, so the rail bridge currently ignores MOVE -- the rail is
 * jogged on the Y axis (see motor_cmd_jog_start). */
void motor_cmd_move_to(int x_mm, int z_mm);
