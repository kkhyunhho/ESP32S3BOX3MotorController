#pragma once

/* Create and register all LVGL widgets.
 * Must be called inside a bsp_display_lock / bsp_display_unlock pair. */
void ui_create(void);

/* Update the live rail-position label (mm). Thread-safe: it is called
 * from the serial RX task, so it takes the display lock internally. */
void ui_rail_set_position(float mm);
