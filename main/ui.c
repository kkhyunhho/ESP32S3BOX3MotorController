#include "ui.h"
#include "motor_cmd.h"
#include "cmd_link.h"
#include "network.h"
#include "lvgl.h"
#include "esp_timer.h"

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

/* ── Status-tab widgets, updated by the 1 Hz refresh timer ──────────── */
static lv_obj_t *s_lbl_wifi_state;
static lv_obj_t *s_lbl_ssid;
static lv_obj_t *s_lbl_ip;
static lv_obj_t *s_lbl_rssi;
static lv_obj_t *s_lbl_mac;
static lv_obj_t *s_lbl_tcp_state;
static lv_obj_t *s_lbl_peer;
static lv_obj_t *s_lbl_last_cmd;
static lv_obj_t *s_rssi_bars[4];

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

/* ── Control tab — original dial + Y placeholder content ────────────── */
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

    make_y_btn(tab, RIGHT_X, 2,                RIGHT_W, Y_BTN_H, "Y " LV_SYMBOL_UP);
    make_y_btn(tab, RIGHT_X, CONTENT_H / 2 + 2, RIGHT_W, Y_BTN_H, "Y " LV_SYMBOL_DOWN);
}

/* ── Status-tab helpers ─────────────────────────────────────────────── */

/* RSSI dBm to a 0..4 bar count, mirroring how phones quantize signal. */
static int rssi_to_bars(int rssi)
{
    if (rssi == 0)           return 0;   /* "not connected" sentinel */
    if (rssi >= -50)         return 4;
    if (rssi >= -65)         return 3;
    if (rssi >= -75)         return 2;
    if (rssi >= -85)         return 1;
    return 0;
}

static const char *rssi_quality_label(int rssi)
{
    int b = rssi_to_bars(rssi);
    switch (b) {
        case 4:  return "excellent";
        case 3:  return "good";
        case 2:  return "fair";
        case 1:  return "weak";
        default: return rssi == 0 ? "—" : "lost";
    }
}

/* Build the 4 phone-style RSSI bars next to the RSSI text. Bars grow
 * in height from left to right; the lit ones take a colour that
 * reflects overall signal quality. */
static void build_rssi_bars(lv_obj_t *parent, int x, int baseline_y)
{
    const int bar_w   = 5;
    const int bar_gap = 3;
    const int heights[4] = { 5, 9, 13, 17 };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, bar_w, heights[i]);
        lv_obj_set_pos(bar,
                       x + i * (bar_w + bar_gap),
                       baseline_y - heights[i]);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COL_OFF), 0);
        s_rssi_bars[i] = bar;
    }
}

static void update_rssi_bars(int rssi)
{
    int lit = rssi_to_bars(rssi);
    uint32_t lit_col = (lit >= 3) ? COL_OK :
                       (lit >= 2) ? COL_WARN :
                                    COL_BAD;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_rssi_bars[i],
            lv_color_hex(i < lit ? lit_col : COL_OFF), 0);
    }
}

/* Add one "Key: value" row at (x, y) and store the value-label pointer
 * so the periodic timer can update it. The key label uses dim text;
 * the value uses bright. */
static void add_kv_row(lv_obj_t *parent, int x, int y,
                       const char *key, lv_obj_t **value_out)
{
    lv_obj_t *k = lv_label_create(parent);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_color(k, lv_color_hex(COL_TEXT_DIM), 0);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(k, x, y);

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, "—");
    lv_obj_set_style_text_color(v, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(v, x + 56, y);
    *value_out = v;
}

/* ── Periodic refresh — 1 Hz LVGL timer ─────────────────────────────── */
static void status_refresh_cb(lv_timer_t *t)
{
    (void)t;
    char buf[40];

    /* Wi-Fi block */
    const char *state_str;
    switch (network_get_state()) {
    case NETWORK_STATE_CONNECTED:    state_str = "CONNECTED";    break;
    case NETWORK_STATE_CONNECTING:   state_str = "CONNECTING…";  break;
    case NETWORK_STATE_DISCONNECTED: state_str = "DISCONNECTED"; break;
    default:                         state_str = "IDLE";         break;
    }
    lv_label_set_text(s_lbl_wifi_state, state_str);

    network_get_ssid(buf, sizeof(buf));
    lv_label_set_text(s_lbl_ssid, buf[0] ? buf : "—");

    network_get_ip(buf, sizeof(buf));
    lv_label_set_text(s_lbl_ip, buf[0] ? buf : "—");

    int rssi = network_get_rssi();
    if (rssi != 0) {
        lv_label_set_text_fmt(s_lbl_rssi, "%d dBm  %s",
                              rssi, rssi_quality_label(rssi));
    } else {
        lv_label_set_text(s_lbl_rssi, "—");
    }
    update_rssi_bars(rssi);

    network_get_mac(buf, sizeof(buf));
    lv_label_set_text(s_lbl_mac, buf[0] ? buf : "—");

    /* TCP block */
    bool has = cmd_link_has_client();
    lv_label_set_text(s_lbl_tcp_state, has ? "bridge.py connected" : "no client");
    lv_obj_set_style_text_color(s_lbl_tcp_state,
        lv_color_hex(has ? COL_OK : COL_BAD), 0);

    cmd_link_get_peer(buf, sizeof(buf));
    lv_label_set_text(s_lbl_peer, buf[0] ? buf : "—");

    int64_t last_us = cmd_link_last_send_us();
    if (last_us > 0) {
        int sec_ago = (int)((esp_timer_get_time() - last_us) / 1000000);
        char cmd_buf[24];
        cmd_link_get_last_cmd(cmd_buf, sizeof(cmd_buf));
        lv_label_set_text_fmt(s_lbl_last_cmd, "%s   %ds ago",
                              cmd_buf, sec_ago);
    } else {
        lv_label_set_text(s_lbl_last_cmd, "—");
    }
}

/* ── Status tab — Wi-Fi / TCP diagnostics ───────────────────────────── */
static void build_status_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    const int row_h = 19;
    int y = 0;

    add_kv_row(tab, 0, y, "Wi-Fi", &s_lbl_wifi_state);  y += row_h;
    add_kv_row(tab, 0, y, "SSID",  &s_lbl_ssid);        y += row_h;
    add_kv_row(tab, 0, y, "IP",    &s_lbl_ip);          y += row_h;
    add_kv_row(tab, 0, y, "RSSI",  &s_lbl_rssi);
    /* baseline_y is the bottom of the tallest bar so they sit on
     * the text baseline of the RSSI row. */
    build_rssi_bars(tab, 230, y + 17);                   y += row_h;
    add_kv_row(tab, 0, y, "MAC",   &s_lbl_mac);          y += row_h + 4;

    /* Thin separator between Wi-Fi and TCP groups. */
    lv_obj_t *sep = lv_obj_create(tab);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, SCR_W - 24, 1);
    lv_obj_set_pos(sep, 0, y);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    y += 6;

    add_kv_row(tab, 0, y, "TCP",   &s_lbl_tcp_state);   y += row_h;
    add_kv_row(tab, 0, y, "Peer",  &s_lbl_peer);        y += row_h;
    add_kv_row(tab, 0, y, "Last",  &s_lbl_last_cmd);

    /* Kick off the 1 Hz refresh — owned by LVGL, runs on its task. */
    lv_timer_create(status_refresh_cb, 1000, NULL);
    /* Populate immediately so the first frame is not all em-dashes. */
    status_refresh_cb(NULL);
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

    lv_obj_t *tab_ctrl = lv_tabview_add_tab(tv, "Control");
    lv_obj_t *tab_stat = lv_tabview_add_tab(tv, "Status");

    build_control_tab(tab_ctrl);
    build_status_tab(tab_stat);
}
