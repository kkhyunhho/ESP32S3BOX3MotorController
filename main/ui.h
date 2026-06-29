#pragma once

#include <stdbool.h>

/* Create and register all LVGL widgets.
 * Must be called inside a bsp_display_lock / bsp_display_unlock pair. */
void ui_create(void);

/* Update the Rail tab: connectivity (rail/host link health) and the
 * rail's displacement from origin in mm (origin = Home / power-on = 0).
 * Thread-safe: called from the serial RX task, so it takes the display
 * lock internally. */
void ui_rail_set_status(bool connected, float mm);
