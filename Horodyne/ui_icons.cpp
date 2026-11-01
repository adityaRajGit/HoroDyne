// Building blocks and icons. Everything is drawn with LVGL shapes, so the
// only typeface on the watch is Unica One.
#include "ui_internal.h"
#include <math.h>

static void plain(lv_obj_t *o) {
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *uiBox(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(parent);
    plain(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t *uiButton(lv_obj_t *parent, int x, int y, int w, int h, lv_event_cb_t cb, void *data) {
    lv_obj_t *o = uiBox(parent, x, y, w, h);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a swipe that starts on a button still navigates
    lv_obj_set_style_opa(o, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, data);
    return o;
}

lv_obj_t *uiLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (font == &font_small || font == &font_label) lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t *uiCircle(lv_obj_t *parent, int cx, int cy, int d, uint32_t color, int border) {
    lv_obj_t *o = uiBox(parent, cx - d / 2, cy - d / 2, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    if (border > 0) {
        lv_obj_set_style_border_width(o, border, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    return o;
}

lv_obj_t *uiRect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t *o = uiBox(parent, x, y, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

lv_obj_t *uiRing(lv_obj_t *parent, int x, int y, int d, int width, uint32_t color, int permille) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_pos(a, x, y);
    lv_obj_set_size(a, d, d);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, permille < 0 ? 0 : (permille > 1000 ? 1000 : permille));
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

lv_obj_t *uiSwitch(lv_obj_t *parent, int x, int y, bool on) {
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, 46, 28);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);    // the whole row is the button
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x39393D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x39393D), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_ACTIVITY), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB);
    uiSetSwitch(sw, on);
    return sw;
}

void uiSetSwitch(lv_obj_t *sw, bool on) {
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else    lv_obj_remove_state(sw, LV_STATE_CHECKED);
}

// ----- icons -----------------------------------------------------------------
static lv_obj_t *arcPart(lv_obj_t *p, int x, int y, int d, int w, uint32_t c, int a0, int a1) {
    lv_obj_t *a = lv_arc_create(p);
    lv_obj_set_pos(a, x, y);
    lv_obj_set_size(a, d, d);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(c), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(a, a0, a1);
    lv_arc_set_angles(a, a0, a1);
    return a;
}

// Dots arranged on a circle: sun rays and gear teeth.
static void dotRing(lv_obj_t *p, int cx, int cy, int r, int d, uint32_t c, int border) {
    for (int i = 0; i < 8; i++) {
        float a = i * (float)M_PI / 4;
        uiCircle(p, cx + (int)roundf(r * cosf(a)), cy + (int)roundf(r * sinf(a)), d, c, border);
    }
}

void iconSun(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    int cx = x + s / 2, cy = y + s / 2;
    uiCircle(p, cx, cy, s * 42 / 100, c, 0);
    dotRing(p, cx, cy, s * 42 / 100, s > 30 ? s * 11 / 100 : 4, c, 0);
}

void iconMoon(lv_obj_t *p, int x, int y, int s, uint32_t c, uint32_t bg) {
    int cx = x + s / 2, cy = y + s / 2;
    uiCircle(p, cx, cy, s * 70 / 100, c, 0);
    uiCircle(p, cx + s * 22 / 100, cy - s * 16 / 100, s * 58 / 100, bg, 0);   // cut-out in the background color
}

void iconCloud(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    uiCircle(p, x + s * 58 / 100, y + s * 45 / 100, s * 50 / 100, c, 0);
    uiCircle(p, x + s * 32 / 100, y + s * 58 / 100, s * 34 / 100, c, 0);
    uiRect(p, x + s * 14 / 100, y + s * 56 / 100, s * 72 / 100, s * 20 / 100, c, s / 10);
}

void iconRain(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    iconCloud(p, x, y - s / 10, s, c);
    for (int i = 0; i < 3; i++) uiRect(p, x + s * (28 + i * 20) / 100, y + s * 74 / 100, s > 30 ? 4 : 2, s * 18 / 100, c, 2);
}

void iconWeather(lv_obj_t *p, int x, int y, int s, int code, bool isDay, uint32_t c, uint32_t bg) {
    if (code == 0)        { if (isDay) iconSun(p, x, y, s, c); else iconMoon(p, x, y, s, c, bg); }
    else if (code <= 48)  iconCloud(p, x, y, s, c);
    else if ((code >= 71 && code <= 77) || code == 85 || code == 86) iconCloud(p, x, y, s, c);
    else                  iconRain(p, x, y, s, c);
}

lv_obj_t *iconBattery(lv_obj_t *p, int x, int y, int s, uint32_t c, int pct) {
    int w = s * 80 / 100, h = s * 46 / 100, top = y + (s - h) / 2;
    lv_obj_t *body = uiBox(p, x, top, w, h);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(c), 0);
    lv_obj_set_style_border_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(body, 4, 0);
    uiRect(p, x + w + 1, top + h / 3, 3, h / 3, c, 1);
    int inner = w - 8;
    int fw = inner * (pct < 0 ? 0 : (pct > 100 ? 100 : pct)) / 100;
    return uiRect(p, x + 4, top + 4, fw < 1 ? 1 : fw, h - 8, c, 2);
}

void iconLevel(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    int cx = x + s / 2, cy = y + s / 2;
    uiCircle(p, cx, cy, s * 80 / 100, c, 3);
    uiCircle(p, cx, cy, s * 26 / 100, c, 0);
}

void iconGear(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    int cx = x + s / 2, cy = y + s / 2;
    uiCircle(p, cx, cy, s * 54 / 100, c, 3);
    dotRing(p, cx, cy, s * 38 / 100, s * 16 / 100, c, 0);
}

void iconSync(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    arcPart(p, x + 2, y + 2, s - 4, 3, c, 30, 300);
    uiCircle(p, x + s / 2 + (int)((s / 2 - 3) * cosf(30 * M_PI / 180)), y + s / 2 + (int)((s / 2 - 3) * sinf(30 * M_PI / 180)), 7, c, 0);
}

void iconTimer(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    int cx = x + s / 2, cy = y + s / 2 + 2;
    uiCircle(p, cx, cy, s * 76 / 100, c, 2);
    uiRect(p, cx - 1, cy - s * 26 / 100, 3, s * 28 / 100, c, 1);
    uiRect(p, cx - s / 6, y, s / 3, 3, c, 1);
}

void iconWifi(lv_obj_t *p, int x, int y, int s, uint32_t c) {
    int cx = x + s / 2, cy = y + s * 86 / 100;
    for (int i = 0; i < 3; i++) {
        int r = s * (78 - i * 25) / 100;
        arcPart(p, cx - r, cy - r, r * 2, 3, c, 225, 315);
    }
    uiCircle(p, cx, cy, 5, c, 0);
}

static void line(lv_obj_t *p, int x, int y, const lv_point_precise_t *pts, uint32_t n, uint32_t c) {
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, n);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(c), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
}

void iconBolt26(lv_obj_t *p, int x, int y, uint32_t c) {
    static const lv_point_precise_t pts[] = { {15, 2}, {5, 15}, {12, 15}, {11, 24}, {21, 11}, {14, 11}, {15, 2} };
    line(p, x, y, pts, 7, c);
}

void iconRaise26(lv_obj_t *p, int x, int y, uint32_t c) {
    static const lv_point_precise_t stem[] = { {13, 23}, {13, 4} };
    static const lv_point_precise_t head[] = { {6, 11}, {13, 4}, {20, 11} };
    line(p, x, y, stem, 2, c);
    line(p, x, y, head, 3, c);
}

void uiSetText(lv_obj_t *label, const char *text) {
    if (!label) return;
    const char *cur = lv_label_get_text(label);
    if (cur && strcmp(cur, text) == 0) return;
    lv_label_set_text(label, text);
}

// ----- formatting -------------------------------------------------------------
void fmtThousands(char *out, size_t n, uint32_t v) {
    if (v >= 1000) snprintf(out, n, "%lu,%03lu", (unsigned long)(v / 1000), (unsigned long)(v % 1000));
    else           snprintf(out, n, "%lu", (unsigned long)v);
}

void fmtSince(char *out, size_t n, time_t t) {
    long m = (long)((time(nullptr) - t) / 60);
    if (t == 0 || m < 0) strlcpy(out, "NOT YET", n);
    else if (m < 1)      strlcpy(out, "JUST NOW", n);
    else if (m < 60)     snprintf(out, n, "%ld MIN AGO", m);
    else                 snprintf(out, n, "%ldH AGO", m / 60);
}
