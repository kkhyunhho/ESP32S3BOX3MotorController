#pragma once
#include "esp_err.h"

typedef enum { AXIS_X, AXIS_Z } axis_t;
typedef enum { DIR_POS, DIR_NEG } dir_t;

/* Configure all motors (mode, group ID, enable). */
esp_err_t motor_ctrl_init(void);

/* Start jogging an axis in the given direction. */
void motor_jog_start(axis_t axis, dir_t dir);

/* Stop jogging an axis. */
void motor_jog_stop(axis_t axis);

/* Send homing command to PC bridge. */
void motor_home(void);
