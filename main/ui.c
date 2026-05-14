#include "ui.h"
#include "motor_cmd.h"
#include "lvgl.h"

/* ── Display dimensions ─────────────────────────────────────────────── */
#define SCR_W  320
#define SCR_H  240

/* ── Dial (X/Z quadrant circle) ─────────────────────────────────────── */
#define LEFT_W  210
#define DIAL_D  200
#define DIAL_X  ((LEFT_W - DIAL_D) / 2)
#define DIAL_Y  ((SCR_H  - DIAL_D) / 2)

/* ── Y placeholder panel ────────────────────────────────────────────── */
#define RIGHT_X  (LEFT_W + 4)
#define RIGHT_W  (SCR_W - LEFT_W - 4)
#define Y_BTN_H  ((SCR_H / 2) - 6)

/* ── Colours ─────────────────────────────────────────────────────────── */
#define COL_BG    0x1A1A2E
#define COL_Z     0x2E86C1
#define COL_Z_PR  0x1A4F72
#define COL_X     0x1E8449
#define COL_X_PR  0x0E6251
#define COL_Y      0xB7770D
#define COL_HOME   0xC0392B
#define COL_TEXT   0xECF0F1
#define HOME_BTN_D 44

/* ── Jogging state ──────────────────────────────────────────────────── */
static axis_t s_active_axis;
static dir_t  s_active_dir;
static bool   s_jogging = false;

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
        { "Z " LV_SYMBOL_UP,    LV_ALIGN_TOP_MID,    0,  12 },
        { "Z " LV_SYMBOL_DOWN,  LV_ALIGN_BOTTOM_MID, 0, -12 },
        { LV_SYMBOL_LEFT " X",  LV_ALIGN_LEFT_MID,   12,  0 },
        { "X " LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, -12,  0 },
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

/* ── Y placeholder button (no motor action) ─────────────────────────── */
static lv_obj_t *make_y_btn(lv_obj_t *parent,
                              lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h,
                              const char *text)
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

    return btn;
}

/* ── Public: build the full UI ──────────────────────────────────────── */
void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *div = lv_obj_create(scr);
    lv_obj_remove_style_all(div);
    lv_obj_set_pos(div, LEFT_W, 0);
    lv_obj_set_size(div, 4, SCR_H);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    create_dial(scr);

    make_y_btn(scr, RIGHT_X, 4,              RIGHT_W, Y_BTN_H, "Y " LV_SYMBOL_UP);
    make_y_btn(scr, RIGHT_X, SCR_H / 2 + 2, RIGHT_W, Y_BTN_H, "Y " LV_SYMBOL_DOWN);
}
