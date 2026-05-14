#pragma once

/* Create and register all LVGL widgets.
 * Must be called inside a bsp_display_lock / bsp_display_unlock pair. */
void ui_create(void);
