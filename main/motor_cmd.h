#pragma once

typedef enum { AXIS_X, AXIS_Z } axis_t;
typedef enum { DIR_POS, DIR_NEG } dir_t;

void motor_cmd_log_banner(void);

void motor_cmd_jog_start(axis_t axis, dir_t dir);

void motor_cmd_jog_stop(axis_t axis);

void motor_cmd_home(void);

/* Absolute coordinate move: both axes go to the given mm targets in
 * parallel on the PC side. Emits "CMD:MOVE X <mm> Z <mm>\n". */
void motor_cmd_move_to(int x_mm, int z_mm);
