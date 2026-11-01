#pragma once
// Shared between ui.cpp, ui_apps.cpp and ui_icons.cpp. Not used outside the UI.
#include <lvgl.h>
#include "ui.h"
#include "config.h"
#include "build_info.h"

#define UI_STR2(x) #x
#define UI_STR(x)  UI_STR2(x)
#define VERSION_TEXT "V" HORODYNE_VERSION "." UI_STR(BUILD_NUMBER)   // "V0.2.7"

LV_FONT_DECLARE(font_title);   // 56 px, only the letters of "HoroDyne"
LV_FONT_DECLARE(font_time);    // 104 px digits
LV_FONT_DECLARE(font_hero);    // 72 px digits % ° - .
LV_FONT_DECLARE(font_value);   // 40 px digits , . K ° % -
LV_FONT_DECLARE(font_label);   // 22 px ASCII
LV_FONT_DECLARE(font_small);   // 16 px ASCII
// Space Mono, clock face only (SpaceMono-OFL.txt)
LV_FONT_DECLARE(font_vx_time);  // 64 px Valorax digits - (personal-use licence: .tools/fonts/valorax)
LV_FONT_DECLARE(font_vx_num);   // 22 px Valorax digits , - A M P
LV_FONT_DECLARE(font_sm_label); // 20 px ASCII
LV_FONT_DECLARE(font_sm_small); // 14 px ASCII + °

// ----- building blocks -----------------------------------------------------
lv_obj_t *uiScreen();                                        // black, not scrollable, swipe handler attached
lv_obj_t *uiBox(lv_obj_t *parent, int x, int y, int w, int h);  // invisible container
lv_obj_t *uiButton(lv_obj_t *parent, int x, int y, int w, int h, lv_event_cb_t cb, void *data);
lv_obj_t *uiLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y, const char *text);
lv_obj_t *uiCircle(lv_obj_t *parent, int cx, int cy, int d, uint32_t color, int border);  // border 0 = filled
lv_obj_t *uiRect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius);
lv_obj_t *uiRing(lv_obj_t *parent, int x, int y, int d, int width, uint32_t color, int permille);
void      uiHeader(lv_obj_t *scr, const char *title, uint32_t color);  // title left, time right
lv_obj_t *uiSwitch(lv_obj_t *parent, int x, int y, bool on);
void      uiSetSwitch(lv_obj_t *sw, bool on);

// ----- icons (all drawn with shapes; size is the square they fill) --------
void iconSun(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconMoon(lv_obj_t *p, int x, int y, int s, uint32_t c, uint32_t bg);
void iconCloud(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconRain(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconWeather(lv_obj_t *p, int x, int y, int s, int code, bool isDay, uint32_t c, uint32_t bg);
lv_obj_t *iconBattery(lv_obj_t *p, int x, int y, int s, uint32_t c, int pct);  // returns the fill bar
void iconLevel(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconGear(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconSync(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconTimer(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconWifi(lv_obj_t *p, int x, int y, int s, uint32_t c);
void iconBolt26(lv_obj_t *p, int x, int y, uint32_t c);   // fixed 26 px (line points are static)
void iconRaise26(lv_obj_t *p, int x, int y, uint32_t c);

// Cyberpunk line glyphs (Figma "Icon Set"): straight strokes on a 24 grid, drawn in one object.
enum Glyph : uint8_t { G_PR, G_MERGE, G_COMMIT, G_CHECK, G_X, G_RUN, G_WAIT, G_EYE, G_GH, G_WIFI, G_REFRESH, G_TILE };
lv_obj_t *uiGlyph(lv_obj_t *parent, Glyph g, int x, int y, int size, uint32_t color);
void      uiSetGlyph(lv_obj_t *obj, Glyph g, uint32_t color);

void      uiSetText(lv_obj_t *label, const char *text);   // skips the redraw when nothing changed

// ----- navigation (ui.cpp) ---------------------------------------------------
void uiGo(ScreenId id, lv_screen_load_anim_t anim);      // only from outside LVGL events
void uiGoLater(ScreenId id, lv_screen_load_anim_t anim); // safe inside event callbacks
void uiRequest(UiRequest r);
extern uint8_t uiStackIndex;

// ----- app screens (ui_apps.cpp). Each builder returns a refresh function. -----
typedef void (*RefreshFn)();
RefreshFn buildActivity(lv_obj_t *scr);
RefreshFn buildWeather(lv_obj_t *scr);
RefreshFn buildBattery(lv_obj_t *scr);
RefreshFn buildLevel(lv_obj_t *scr);
RefreshFn buildTimer(lv_obj_t *scr);
RefreshFn buildStopwatch(lv_obj_t *scr);
RefreshFn buildAbout(lv_obj_t *scr);
RefreshFn buildGithub(lv_obj_t *scr);
RefreshFn buildGhPrs(lv_obj_t *scr);
RefreshFn buildGhPr(lv_obj_t *scr);
RefreshFn buildGhRuns(lv_obj_t *scr);
RefreshFn buildGhRun(lv_obj_t *scr);
RefreshFn buildGhCommits(lv_obj_t *scr);
RefreshFn buildSettings(lv_obj_t *scr);
RefreshFn buildWifi(lv_obj_t *scr);
void      appsLeaving();     // stops the fast refresh timer of Level, Timer and Stopwatch

// ----- formatting shared by several screens ---------------------------------
void fmtThousands(char *out, size_t n, uint32_t v);   // 6240 -> "6,240"
void ghStateGlyph(uint8_t state, Glyph &g, uint32_t &color);   // GhState -> glyph + colour
void fmtSince(char *out, size_t n, time_t t);         // "2H AGO"
