#include "ui.h"
#include "motor_cmd.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

#include <stdint.h>
#include <stdio.h>

/* ── Display dimensions ─────────────────────────────────────────────── */
#define SCR_W  320
#define SCR_H  240

/* Tab bar steals a strip from the top; the rest is per-tab content. */
#define TAB_BAR_H  28
#define CONTENT_H  (SCR_H - TAB_BAR_H)

/* ── Dial (X/Z quadrant circle) — sizes are relative to CONTENT_H now. */
#define LEFT_W  210
#define DIAL_D  180
#define DIAL_X  ((LEFT_W - DIAL_D) / 2)
#define DIAL_Y  ((CONTENT_H - DIAL_D) / 2)

/* ── Y placeholder panel ────────────────────────────────────────────── */
#define RIGHT_X  (LEFT_W + 4)
#define RIGHT_W  (SCR_W - LEFT_W - 4)
#define Y_BTN_H  ((CONTENT_H / 2) - 6)

/* ── Colours ─────────────────────────────────────────────────────────── */
#define COL_BG       0x1A1A2E
#define COL_Z        0x2E86C1
#define COL_Z_PR     0x1A4F72
#define COL_X        0x1E8449
#define COL_X_PR     0x0E6251
#define COL_Y        0xB7770D
#define COL_HOME     0xC0392B
#define COL_TEXT     0xECF0F1
#define COL_TEXT_DIM 0x808896
#define COL_OK       0x4CAF50  /* RSSI: strong / TCP: connected */
#define COL_WARN     0xFFC107  /* RSSI: medium                  */
#define COL_BAD      0xE53935  /* RSSI: weak / TCP: no client   */
#define COL_OFF      0x404858  /* unlit RSSI bar                */
#define HOME_BTN_D 44

/* ── Jogging state ──────────────────────────────────────────────────── */
static axis_t s_active_axis;
static dir_t  s_active_dir;
static bool   s_jogging = false;

/* ── Y-jog (rail) state — the Y buttons drive the rail (the rail is the
 *    Y axis); guards a stop without a matching start. ──────────────── */
static bool   s_y_jogging = false;

/* ── Rail tab — connectivity + displacement-from-origin labels,
 *    updated from the serial RX task via ui_rail_set_status(). ─────── */
static lv_obj_t *s_lbl_conn;
static lv_obj_t *s_lbl_disp;

/* Run a block under the display lock with a short timeout, skipping if
 * the lock cannot be taken so the RX task never stalls. */
#define UI_WITH_LOCK(BLOCK)         \
    do {                            \
        if (bsp_display_lock(50)) { \
            BLOCK;                  \
            bsp_display_unlock();   \
        }                           \
    } while (0)

/* ── Move-tab geometry ──────────────────────────────────────────────── */
/* The plot lives in the LEFT pane of the tab (mirrors Jog Control's
 * dial-on-left / placeholder-on-right split). Sized 140 × 140 so it
 * fits with the X/Z readouts, the Move button, AND the current-position
 * line pinned to the bottom. 140 is a multiple of 20 mm so the grid
 * lines still snap to whole pixels (7 px per 20 mm step). */
#define PLOT_SIZE       140
#define PLOT_X_OFFSET   25    /* centres the plot in the left 210 px pane */
#define PLOT_Y_OFFSET   4
#define MARKER_R        6     /* radius of the cross-hair centre dot     */
#define GRID_STEP_MM    20    /* minor grid spacing                       */
#define GRID_MAJOR_MM   100   /* major grid every 5 minor steps           */

/* Must match mks_motor.MKSMotor._max_travel_mm on the PC side; the
 * exact mechanical travel is not known so we use the same 400 mm
 * placeholder both ends. */
#define MAX_TRAVEL_MM   400

/* ── Move-tab widgets + state ───────────────────────────────────────── */
static lv_obj_t *s_plot;
static lv_obj_t *s_v_line;        /* vertical (Z) line — X position */
static lv_obj_t *s_h_line;        /* horizontal (X) line — Z position */
static lv_obj_t *s_marker;        /* small dot at the intersection */
static lv_obj_t *s_lbl_target_x;
static lv_obj_t *s_lbl_target_z;
static int s_target_x_mm = 0;
static int s_target_z_mm = 0;

/* ── Current-position readout (one per control tab) ──────────────────── */
/* Both pinned to the tab bottom, refreshed from the PC's "POS:..."
 * feedback by pos_refresh_cb. */
static lv_obj_t *s_lbl_pos_move;
static lv_obj_t *s_lbl_pos_jog;

/* ── Draw one filled arc sector (angles: 0=3 o'clock, CW, degrees) ──── */
static void draw_sector(lv_layer_t *layer,
                        int32_t cx, int32_t cy, int32_t r,
                        int32_t start_angle, int32_t end_angle,
                        uint32_t colour)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center.x   = cx;
    dsc.center.y   = cy;
    dsc.radius     = (uint16_t)r;
    dsc.width      = (uint16_t)r;   /* fills from centre to circumference */
    dsc.start_angle = start_angle;
    dsc.end_angle   = end_angle;
    dsc.color      = lv_color_hex(colour);
    dsc.opa        = LV_OPA_COVER;
    lv_draw_arc(layer, &dsc);
}

/* ── Custom draw: 4 arc sectors + diagonal dividers ─────────────────── */
static void dial_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t   *obj   = lv_event_get_target(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int32_t cx = (a.x1 + a.x2) / 2;
    int32_t cy = (a.y1 + a.y2) / 2;
    int32_t r  = (a.x2 - a.x1) / 2;

    bool pressed = lv_obj_has_state(obj, LV_STATE_PRESSED);

    uint32_t z_top = (pressed && s_active_axis == AXIS_Z && s_active_dir == DIR_POS)
                     ? COL_Z_PR : COL_Z;
    uint32_t z_bot = (pressed && s_active_axis == AXIS_Z && s_active_dir == DIR_NEG)
                     ? COL_Z_PR : COL_Z;
    uint32_t x_rgt = (pressed && s_active_axis == AXIS_X && s_active_dir == DIR_POS)
                     ? COL_X_PR : COL_X;
    uint32_t x_lft = (pressed && s_active_axis == AXIS_X && s_active_dir == DIR_NEG)
                     ? COL_X_PR : COL_X;

    /* 0° = 3 o'clock in this LVGL build → shift all sectors -90°.
     * Z+ top    : 225°→315° */
    draw_sector(layer, cx, cy, r, 225, 315, z_top);
    /* X+ right  : 315°→360° + 0°→45° (split to handle 0° wrap) */
    draw_sector(layer, cx, cy, r, 315, 360, x_rgt);
    draw_sector(layer, cx, cy, r,   0,  45, x_rgt);
    /* Z- bottom : 45°→135° */
    draw_sector(layer, cx, cy, r,  45, 135, z_bot);
    /* X- left   : 135°→225° */
    draw_sector(layer, cx, cy, r, 135, 225, x_lft);

    /* Diagonal divider lines (center ↔ circle edge at 45° multiples) */
    int32_t d = r * 707 / 1000;  /* r / √2 */
    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    ln.color = lv_color_hex(COL_BG);
    ln.width = 3;
    ln.opa   = LV_OPA_COVER;
    ln.p1.x = cx - d;  ln.p1.y = cy - d;
    ln.p2.x = cx + d;  ln.p2.y = cy + d;
    lv_draw_line(layer, &ln);
    ln.p1.x = cx + d;  ln.p1.y = cy - d;
    ln.p2.x = cx - d;  ln.p2.y = cy + d;
    lv_draw_line(layer, &ln);
}

/* ── Diagonal hit-test ──────────────────────────────────────────────── */
static void decode_touch(lv_event_t *e, lv_obj_t *obj,
                         axis_t *axis, dir_t *dir)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) indev = lv_indev_active();

    lv_point_t pt = {0, 0};
    if (indev) lv_indev_get_point(indev, &pt);

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int32_t dx = pt.x - (area.x1 + area.x2) / 2;
    int32_t dy = pt.y - (area.y1 + area.y2) / 2;

    if (LV_ABS(dy) >= LV_ABS(dx)) {
        *axis = AXIS_Z;
        *dir  = (dy < 0) ? DIR_POS : DIR_NEG;
    } else {
        *axis = AXIS_X;
        *dir  = (dx > 0) ? DIR_POS : DIR_NEG;
    }
}

/* ── Dial touch event callback ──────────────────────────────────────── */
static void dial_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t       *obj  = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        axis_t axis;
        dir_t  dir;
        decode_touch(e, obj, &axis, &dir);
        s_active_axis = axis;
        s_active_dir  = dir;
        s_jogging     = true;
        lv_obj_invalidate(obj);
        motor_cmd_jog_start(axis, dir);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        /* s_jogging guards against sending a stop command when no jog
         * was actually started — LVGL can fire RELEASED / PRESS_LOST
         * without a preceding PRESSED if the touch starts outside the
         * widget and drifts in, or after a programmatic state change. */
        if (s_jogging) {
            motor_cmd_jog_stop(s_active_axis);
            s_jogging = false;
            lv_obj_invalidate(obj);
        }
    }
}

/* ── Home button callback ───────────────────────────────────────────── */
static void home_event_cb(lv_event_t *e)
{
    (void)e;
    motor_cmd_home();
}

/* ── Build the dial button ──────────────────────────────────────────── */
static lv_obj_t *create_dial(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, DIAL_D, DIAL_D);
    lv_obj_set_pos(btn, DIAL_X, DIAL_Y);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    /* Background colour matches screen — arc sectors define the visual circle */
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BG), LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(btn, dial_draw_cb,  LV_EVENT_DRAW_MAIN,  NULL);
    lv_obj_add_event_cb(btn, dial_event_cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(btn, dial_event_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(btn, dial_event_cb, LV_EVENT_PRESS_LOST, NULL);

    static const struct {
        const char *text;
        lv_align_t  align;
        int32_t     ox, oy;
    } labels[4] = {
        { "Z " LV_SYMBOL_UP,    LV_ALIGN_TOP_MID,    0,  8 },
        { "Z " LV_SYMBOL_DOWN,  LV_ALIGN_BOTTOM_MID, 0, -8 },
        { LV_SYMBOL_LEFT " X",  LV_ALIGN_LEFT_MID,   8,  0 },
        { "X " LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, -8,  0 },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, labels[i].text);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_align_to(lbl, btn, labels[i].align, labels[i].ox, labels[i].oy);
    }

    /* Small home button at the dial centre */
    lv_obj_t *hbtn = lv_button_create(parent);
    lv_obj_set_size(hbtn, HOME_BTN_D, HOME_BTN_D);
    lv_obj_align_to(hbtn, btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(hbtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hbtn, lv_color_hex(COL_HOME), 0);
    lv_obj_set_style_bg_color(hbtn,
        lv_color_hex(COL_HOME >> 1 & 0x7F7F7F), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(hbtn, 0, 0);
    lv_obj_set_style_shadow_width(hbtn, 0, 0);
    lv_obj_add_event_cb(hbtn, home_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hlbl = lv_label_create(hbtn);
    lv_label_set_text(hlbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(hlbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(hlbl);

    return btn;
}

/* ── Y jog button — drives the linear rail (the rail is the Y axis) ───
 * X/Z are reserved for the future ball-screw motors, so only Y jogs the
 * rail. The jog direction is carried in the event user data. */
static void y_jog_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    dir_t dir = (dir_t)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSED) {
        s_y_jogging = true;
        motor_cmd_jog_start(AXIS_Y, dir);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_y_jogging) {
            motor_cmd_jog_stop(AXIS_Y);
            s_y_jogging = false;
        }
    }
}

static lv_obj_t *make_y_btn(lv_obj_t *parent,
                            lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_coord_t h,
                            const char *text, dir_t dir)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_Y), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn,
        lv_color_hex(COL_Y >> 1 & 0x7F7F7F), LV_STATE_PRESSED);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, y_jog_event_cb, LV_EVENT_PRESSED,
                        (void *)(intptr_t)dir);
    lv_obj_add_event_cb(btn, y_jog_event_cb, LV_EVENT_RELEASED,
                        (void *)(intptr_t)dir);
    lv_obj_add_event_cb(btn, y_jog_event_cb, LV_EVENT_PRESS_LOST,
                        (void *)(intptr_t)dir);
    return btn;
}

/* ── Jog tab — X/Z dial (reserved for the future ball-screw) plus the
 *    Y jog buttons that drive the rail. ─────────────────────────────── */
/* One-line current-position readout pinned to the bottom-left of a
 * control tab. Auto-sized (no fixed width) so it always stays one line;
 * the values come from pos_refresh_cb. */
static lv_obj_t *make_pos_label(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, 6, CONTENT_H - 16);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, "Pos  --");
    return lbl;
}

static void build_control_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *div = lv_obj_create(tab);
    lv_obj_remove_style_all(div);
    lv_obj_set_pos(div, LEFT_W, 0);
    lv_obj_set_size(div, 4, CONTENT_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    create_dial(tab);

    make_y_btn(tab, RIGHT_X, 2, RIGHT_W, Y_BTN_H,
               "Y " LV_SYMBOL_UP, DIR_POS);
    make_y_btn(tab, RIGHT_X, CONTENT_H / 2 + 2, RIGHT_W, Y_BTN_H,
               "Y " LV_SYMBOL_DOWN, DIR_NEG);

    s_lbl_pos_jog = make_pos_label(tab);
}

/* ── Rail tab — connectivity + displacement-from-origin readout ──────
 * The rail is the Y axis. This tab shows whether the host bridge / rail
 * link is alive and how far the rail has moved from the origin (Home /
 * power-on = 0 mm). Both labels are updated from the serial RX task via
 * ui_rail_set_status(). */
static void build_rail_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "Linear Rail (Y)");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT_DIM), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* Connectivity: green "Connected" / red "Disconnected". Starts
     * disconnected until the first STAT frame arrives. */
    s_lbl_conn = lv_label_create(tab);
    lv_label_set_text(s_lbl_conn, "Disconnected");
    lv_obj_set_style_text_color(s_lbl_conn, lv_color_hex(COL_BAD), 0);
    lv_obj_set_style_text_font(s_lbl_conn, &lv_font_montserrat_18, 0);
    lv_obj_align(s_lbl_conn, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t *disp_cap = lv_label_create(tab);
    lv_label_set_text(disp_cap, "From origin");
    lv_obj_set_style_text_color(disp_cap, lv_color_hex(COL_TEXT_DIM), 0);
    lv_obj_set_style_text_font(disp_cap, &lv_font_montserrat_14, 0);
    lv_obj_align(disp_cap, LV_ALIGN_CENTER, 0, 16);

    s_lbl_disp = lv_label_create(tab);
    lv_label_set_text(s_lbl_disp, "— mm");
    lv_obj_set_style_text_color(s_lbl_disp, lv_color_hex(COL_Y), 0);
    lv_obj_set_style_text_font(s_lbl_disp, &lv_font_montserrat_18, 0);
    lv_obj_align(s_lbl_disp, LV_ALIGN_CENTER, 0, 40);
}

/* ── Move tab — 2D drag-to-target picker ────────────────────────────── */

/* Draw 20 mm grid + every-5th-line major emphasis on top of the plot
 * background. Runs from LV_EVENT_DRAW_MAIN so the lines layer over the
 * plot bg / border but under any sibling crosshair widgets. */
static void plot_grid_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t   *obj   = lv_event_get_target(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 1;
    line.opa   = LV_OPA_COVER;

    for (int mm = GRID_STEP_MM; mm < MAX_TRAVEL_MM; mm += GRID_STEP_MM) {
        bool major = (mm % GRID_MAJOR_MM == 0);
        line.color = lv_color_hex(major ? 0x4D5278 : 0x2D3050);

        int px = mm * PLOT_SIZE / MAX_TRAVEL_MM;

        /* Vertical grid line at this X offset. */
        line.p1.x = a.x1 + px;  line.p1.y = a.y1;
        line.p2.x = a.x1 + px;  line.p2.y = a.y2;
        lv_draw_line(layer, &line);

        /* Horizontal grid line at the same offset (square plot). */
        line.p1.x = a.x1;       line.p1.y = a.y1 + px;
        line.p2.x = a.x2;       line.p2.y = a.y1 + px;
        lv_draw_line(layer, &line);
    }
}

/* Re-position the crosshair widgets + numeric labels for the current
 * (s_target_x_mm, s_target_z_mm).
 *
 * The plot origin (0,0) lives at the TOP-RIGHT corner of the square
 * so the on-screen drag direction matches the real mechanical motion
 * (X grows leftwards, Z grows downwards). Pixel origin in LVGL is
 * still top-left so both axes get inverted explicitly. */
static void refresh_move_widgets(void)
{
    int px_x = PLOT_SIZE - s_target_x_mm * PLOT_SIZE / MAX_TRAVEL_MM;
    int px_y = s_target_z_mm * PLOT_SIZE / MAX_TRAVEL_MM;

    lv_obj_set_pos(s_v_line, PLOT_X_OFFSET + px_x - 1, PLOT_Y_OFFSET);
    lv_obj_set_pos(s_h_line, PLOT_X_OFFSET,            PLOT_Y_OFFSET + px_y - 1);
    lv_obj_set_pos(s_marker,
                   PLOT_X_OFFSET + px_x - MARKER_R,
                   PLOT_Y_OFFSET + px_y - MARKER_R);

    lv_label_set_text_fmt(s_lbl_target_x, "X: %3d mm", s_target_x_mm);
    lv_label_set_text_fmt(s_lbl_target_z, "Z: %3d mm", s_target_z_mm);
}

static void plot_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    if (indev == NULL) {
        return;
    }

    lv_point_t pt = {0, 0};
    lv_indev_get_point(indev, &pt);

    lv_area_t area;
    lv_obj_get_coords(s_plot, &area);
    int px = pt.x - area.x1;
    int py = pt.y - area.y1;

    if (px < 0) px = 0;
    if (px > PLOT_SIZE) px = PLOT_SIZE;
    if (py < 0) py = 0;
    if (py > PLOT_SIZE) py = PLOT_SIZE;

    /* Both axes inverted so the on-screen drag matches the real
     * mechanical direction (validated against the actual rig). */
    s_target_x_mm = (PLOT_SIZE - px) * MAX_TRAVEL_MM / PLOT_SIZE;
    s_target_z_mm = py * MAX_TRAVEL_MM / PLOT_SIZE;

    refresh_move_widgets();
}

static void confirm_event_cb(lv_event_t *e)
{
    (void)e;
    motor_cmd_move_to(s_target_x_mm, s_target_z_mm);
}

static void build_move_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    /* Same left/right split as Jog Control: the X/Z plot lives in the
     * left LEFT_W=210 px column, with a thin vertical divider, and
     * the right column is reserved as the linear-motor placeholder. */
    lv_obj_t *div = lv_obj_create(tab);
    lv_obj_remove_style_all(div);
    lv_obj_set_pos(div, LEFT_W, 0);
    lv_obj_set_size(div, 4, CONTENT_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    /* The square drag area. Borders + dim background so it reads as a
     * "plottable surface" rather than a button. */
    s_plot = lv_obj_create(tab);
    lv_obj_remove_style_all(s_plot);
    lv_obj_set_size(s_plot, PLOT_SIZE, PLOT_SIZE);
    lv_obj_set_pos(s_plot, PLOT_X_OFFSET, PLOT_Y_OFFSET);
    lv_obj_set_style_bg_color(s_plot, lv_color_hex(0x22253A), 0);
    lv_obj_set_style_bg_opa(s_plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_plot, lv_color_hex(0x4D5278), 0);
    lv_obj_set_style_border_width(s_plot, 1, 0);
    lv_obj_set_style_radius(s_plot, 0, 0);
    lv_obj_add_flag(s_plot, LV_OBJ_FLAG_CLICKABLE);
    /* Stop the tabview from interpreting horizontal drag as a tab
     * swipe — we want it to feed the plot's PRESSING event instead. */
    lv_obj_clear_flag(s_plot, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(s_plot, LV_OBJ_FLAG_SCROLLABLE);

    /* 20 mm grid drawn by a DRAW_MAIN callback (no widget overhead). */
    lv_obj_add_event_cb(s_plot, plot_grid_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    /* Cross-hair: a thin vertical bar (Z axis line) at the target X
     * and a thin horizontal bar (X axis line) at the target Z. They
     * are children of the tab (not s_plot) so they can be repositioned
     * with absolute set_pos against the same coordinate origin. */
    s_v_line = lv_obj_create(tab);
    lv_obj_remove_style_all(s_v_line);
    lv_obj_set_size(s_v_line, 2, PLOT_SIZE);
    lv_obj_set_style_bg_color(s_v_line, lv_color_hex(COL_Z), 0);
    lv_obj_set_style_bg_opa(s_v_line, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_v_line, LV_OBJ_FLAG_IGNORE_LAYOUT);

    s_h_line = lv_obj_create(tab);
    lv_obj_remove_style_all(s_h_line);
    lv_obj_set_size(s_h_line, PLOT_SIZE, 2);
    lv_obj_set_style_bg_color(s_h_line, lv_color_hex(COL_X), 0);
    lv_obj_set_style_bg_opa(s_h_line, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_h_line, LV_OBJ_FLAG_IGNORE_LAYOUT);

    s_marker = lv_obj_create(tab);
    lv_obj_remove_style_all(s_marker);
    lv_obj_set_size(s_marker, MARKER_R * 2, MARKER_R * 2);
    lv_obj_set_style_radius(s_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_marker, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_opa(s_marker, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_marker, LV_OBJ_FLAG_IGNORE_LAYOUT);

    /* Plot grabs the touch events; the crosshair widgets above sit on
     * top of it visually but don't intercept touch. */
    lv_obj_add_event_cb(s_plot, plot_event_cb, LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_plot, plot_event_cb, LV_EVENT_PRESSING, NULL);

    /* Under-plot readouts + Move button — all inside the left pane. */
    const int below_y = PLOT_Y_OFFSET + PLOT_SIZE + 6;

    s_lbl_target_x = lv_label_create(tab);
    lv_obj_set_style_text_color(s_lbl_target_x, lv_color_hex(COL_X), 0);
    lv_obj_set_style_text_font(s_lbl_target_x, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_lbl_target_x, 10, below_y);

    s_lbl_target_z = lv_label_create(tab);
    lv_obj_set_style_text_color(s_lbl_target_z, lv_color_hex(COL_Z), 0);
    lv_obj_set_style_text_font(s_lbl_target_z, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(s_lbl_target_z, 10, below_y + 18);

    lv_obj_t *confirm_btn = lv_button_create(tab);
    lv_obj_set_size(confirm_btn, 85, 36);
    lv_obj_set_pos(confirm_btn, 115, below_y + 2);
    lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(COL_OK), 0);
    lv_obj_set_style_bg_color(confirm_btn,
        lv_color_hex(COL_OK >> 1 & 0x7F7F7F), LV_STATE_PRESSED);
    lv_obj_set_style_radius(confirm_btn, 8, 0);
    lv_obj_set_style_border_width(confirm_btn, 0, 0);
    lv_obj_set_style_shadow_width(confirm_btn, 0, 0);
    lv_obj_add_event_cb(confirm_btn, confirm_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *confirm_lbl = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_lbl, LV_SYMBOL_OK "  Move");
    lv_obj_set_style_text_color(confirm_lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(confirm_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(confirm_lbl);

    /* Right pane reserved for the future linear-motor controls. Render
     * a single dim box with "Linear (TBD)" so the user can see the
     * region is intentionally claimed but not implemented yet. Mirrors
     * Jog Control's two-Y-button placement so the two tabs feel
     * structurally similar. */
    lv_obj_t *linear = lv_obj_create(tab);
    lv_obj_remove_style_all(linear);
    lv_obj_set_pos(linear, RIGHT_X, 6);
    lv_obj_set_size(linear, RIGHT_W, CONTENT_H - 12);
    lv_obj_set_style_bg_color(linear, lv_color_hex(0x22253A), 0);
    lv_obj_set_style_bg_opa(linear, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(linear, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_border_width(linear, 1, 0);
    lv_obj_set_style_radius(linear, 8, 0);

    lv_obj_t *linear_lbl = lv_label_create(linear);
    lv_label_set_text(linear_lbl, "Linear\n(TBD)");
    lv_obj_set_style_text_align(linear_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(linear_lbl, lv_color_hex(COL_TEXT_DIM), 0);
    lv_obj_set_style_text_font(linear_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(linear_lbl);

    s_lbl_pos_move = make_pos_label(tab);

    refresh_move_widgets();
}

/* Refresh both control tabs' current-position lines from the latest
 * "POS:X <mm> Z <mm>" the PC pushed. "Pos  --" until the first update
 * (or after the link drops, which clears the stored value). */
static void pos_refresh_cb(lv_timer_t *t)
{
    (void)t;
    char pos[32];
    cmd_link_get_position(pos, sizeof(pos));

    char buf[40];
    if (pos[0] == '\0') {
        snprintf(buf, sizeof(buf), "Pos  --");
    } else {
        snprintf(buf, sizeof(buf), "Pos  %s mm", pos);
    }

    if (s_lbl_pos_move != NULL) {
        lv_label_set_text(s_lbl_pos_move, buf);
    }
    if (s_lbl_pos_jog != NULL) {
        lv_label_set_text(s_lbl_pos_jog, buf);
    }
}

/* ── Public: build the full UI ──────────────────────────────────────── */
void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *tv = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_position(tv, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tv, TAB_BAR_H);
    lv_obj_set_style_bg_color(tv, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_color(tv, lv_color_hex(COL_TEXT), 0);

    /* Disable horizontal swipe between tabs — tab buttons remain the
     * only way to switch. set_active() drives the programmatic scroll
     * itself so the snap behavior is preserved when buttons are tapped. */
    lv_obj_clear_flag(lv_tabview_get_content(tv), LV_OBJ_FLAG_SCROLLABLE);

    /* Tab order: Move Control (left) | Jog Control (middle) | Rail (right). */
    lv_obj_t *tab_move = lv_tabview_add_tab(tv, "Move Control");
    lv_obj_t *tab_ctrl = lv_tabview_add_tab(tv, "Jog Control");
    lv_obj_t *tab_rail = lv_tabview_add_tab(tv, "Rail");

    build_move_tab(tab_move);
    build_control_tab(tab_ctrl);
    build_rail_tab(tab_rail);

    /* Drive both control tabs' position readouts at 2 Hz (matches the
     * bridge's ~500 ms POS push). */
    lv_timer_create(pos_refresh_cb, 500, NULL);

    /* Default landing tab — keep Jog Control front since that's the
     * primary controller; user can swipe-by-tab to Move/Rail. */
    lv_tabview_set_active(tv, 1, LV_ANIM_OFF);
}

/* Update the Rail tab from the serial RX task (takes the display lock
 * internally). The rail is the Y axis and the origin is Home (0 mm), so
 * the reported position is the displacement from origin. */
void ui_rail_set_status(bool connected, float mm)
{
    UI_WITH_LOCK({
        if (s_lbl_conn) {
            lv_label_set_text(s_lbl_conn,
                              connected ? "Connected" : "Disconnected");
            lv_obj_set_style_text_color(s_lbl_conn,
                lv_color_hex(connected ? COL_OK : COL_BAD), 0);
        }
        if (s_lbl_disp) {
            lv_label_set_text_fmt(s_lbl_disp, "%.1f mm", mm);
        }
    });
}
