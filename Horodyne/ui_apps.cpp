// The seven apps and the Wi-Fi setup screen.
#include "ui_internal.h"
#include "svc_clock.h"
#include "svc_settings.h"
#include "svc_activity.h"
#include "svc_net.h"
#include "hal_battery.h"
#include "hal_display.h"
#include "hal_imu.h"
#include "svc_timer.h"
#include <math.h>

static lv_obj_t *centeredColumn(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *c = uiBox(parent, x, y, w, h);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return c;
}

static lv_obj_t *rightLabel(lv_obj_t *parent, const lv_font_t *f, uint32_t c, int x, int y, int w) {
    lv_obj_t *l = uiLabel(parent, f, c, x, y, "");
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    return l;
}

// ----- Activity --------------------------------------------------------------------
static lv_obj_t *aRing, *aSteps, *aGoal, *aDist, *aBars[12];

static void activityRefresh() {
    char b[24];
    fmtThousands(b, sizeof(b), activity.steps);
    uiSetText(aSteps, b);
    lv_arc_set_value(aRing, settings.stepGoal ? (int)(activity.steps * 1000UL / settings.stepGoal) : 0);
    fmtThousands(b, sizeof(b), settings.stepGoal);
    uiSetText(aGoal, b);
    snprintf(b, sizeof(b), "%.1f KM", activityKm());
    uiSetText(aDist, b);
    uint16_t mx = 1;
    for (int i = 0; i < 12; i++) if (activity.bins[i] > mx) mx = activity.bins[i];
    for (int i = 0; i < 12; i++) {
        int h = LV_MAX(2, activity.bins[i] * 40 / mx);
        lv_obj_set_height(aBars[i], h);
        lv_obj_set_y(aBars[i], 212 - h);
        lv_obj_set_style_bg_color(aBars[i], lv_color_hex(activity.bins[i] ? COL_ACTIVITY : COL_FILL), 0);
    }
}

RefreshFn buildActivity(lv_obj_t *s) {
    uiHeader(s, "ACTIVITY", COL_ACTIVITY);
    aRing = uiRing(s, 20, 50, 150, 16, COL_ACTIVITY, 0);
    lv_obj_t *col = centeredColumn(s, 20, 50, 150, 150);
    aSteps = uiLabel(col, &font_value, COL_TEXT, 0, 0, "");
    uiLabel(col, &font_small, COL_GREY, 0, 0, "STEPS");
    uiLabel(s, &font_small, COL_GREY, 186, 54, "GOAL");
    aGoal = uiLabel(s, &font_label, COL_TEXT, 186, 72, "");
    uiLabel(s, &font_small, COL_GREY, 186, 106, "DIST");
    aDist = uiLabel(s, &font_label, COL_TEXT, 186, 124, "");
    for (int i = 0; i < 12; i++) aBars[i] = uiRect(s, 186 + i * 6, 210, 4, 2, COL_FILL, 2);
    return activityRefresh;
}

// ----- Weather ------------------------------------------------------------------------
static lv_obj_t *wAge;

static void weatherRefresh() {
    if (!wAge) return;
    char ago[16], b[48];
    fmtSince(ago, sizeof(ago), weather.fetchedAt);
    if (weather.valid) snprintf(b, sizeof(b), "H %d°  L %d°   %s", weather.hi, weather.lo, ago);
    else               strlcpy(b, settings.city[0] ? "CONNECTS EVERY 6 HOURS" : "SET A CITY IN SETTINGS", sizeof(b));
    uiSetText(wAge, b);
}

RefreshFn buildWeather(lv_obj_t *s) {
    uiHeader(s, settings.city[0] ? settings.city : "WEATHER", COL_WEATHER);
    char b[24];
    if (weather.valid) snprintf(b, sizeof(b), "%d°", weather.temp); else strlcpy(b, "--°", sizeof(b));
    uiLabel(s, &font_hero, COL_TEXT, 22, 38, b);
    lv_obj_t *ib = uiBox(s, 186, 40, 64, 64);
    iconWeather(ib, 0, 0, 64, weather.valid ? weather.code : 0, weather.valid ? weather.isDay : true, COL_WEATHER, COL_BG);
    uiLabel(s, &font_label, COL_TEXT, 24, 98, weather.valid ? weatherCondition(weather.code) : "NO DATA YET");
    wAge = uiLabel(s, &font_small, COL_GREY, 24, 128, "");

    if (weather.valid) {
        for (int i = 0; i < 4; i++) {
            int cx = 50 + i * 60;
            lv_obj_t *t = uiLabel(s, &font_small, COL_GREY, 0, 0, weather.hLabel[i]);
            lv_obj_align(t, LV_ALIGN_TOP_LEFT, cx - 20, 160);
            lv_obj_set_width(t, 40);
            lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_t *hb = uiBox(s, cx - 10, 182, 20, 20);
            iconWeather(hb, 0, 0, 20, weather.hCode[i], weather.hDay[i], weather.hDay[i] ? COL_WEATHER : COL_GREY, COL_BG);
            snprintf(b, sizeof(b), "%d°", weather.hTemp[i]);
            lv_obj_t *v = uiLabel(s, &font_label, COL_TEXT, cx - 24, 206, b);
            lv_obj_set_width(v, 48);
            lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    return weatherRefresh;
}

// ----- Battery ------------------------------------------------------------------------
static lv_obj_t *bPct, *bState, *bVolts, *bDays, *bFill, *bSwitch;

static void batteryRefresh() {
    char b[24];
    if (battery.present) snprintf(b, sizeof(b), "%u%%", battery.percent); else strlcpy(b, "--%", sizeof(b));
    uiSetText(bPct, b);
    uiSetText(bState, !battery.present ? "NO BATTERY" : (battery.charging ? "CHARGING" : "ON BATTERY"));
    snprintf(b, sizeof(b), "%.2f V", battery.volts);
    uiSetText(bVolts, b);
    if (battery.daysLeft > 0) snprintf(b, sizeof(b), "~%d DAYS", (int)roundf(battery.daysLeft));
    else                      strlcpy(b, "-- DAYS", sizeof(b));
    uiSetText(bDays, b);
    bool warn = battery.present && !battery.charging && battery.percent <= BAT_WARN_PCT;
    lv_obj_set_width(bFill, LV_MAX(4, 236 * (battery.present ? battery.percent : 0) / 100));
    lv_obj_set_style_bg_color(bFill, lv_color_hex(warn ? COL_WARN : (battery.charging ? COL_CHARGE : COL_BATTERY)), 0);
    uiSetSwitch(bSwitch, settings.lowPower);
}

static void lowPowerTap(lv_event_t *) {
    settings.lowPower = !settings.lowPower;
    settingsSave();
    displaySetBacklight(uiBrightnessPercent());
    batteryRefresh();
}

RefreshFn buildBattery(lv_obj_t *s) {
    uiHeader(s, "BATTERY", COL_BATTERY);
    bPct   = uiLabel(s, &font_hero, COL_TEXT, 22, 38, "");
    bState = rightLabel(s, &font_small, COL_GREY, 140, 46, 116);
    bVolts = rightLabel(s, &font_label, COL_TEXT, 140, 66, 116);
    bDays  = rightLabel(s, &font_label, COL_TEXT, 140, 92, 116);
    uiRect(s, 22, 128, 236, 20, 0x3A3315, 10);
    bFill = uiRect(s, 22, 128, 4, 20, COL_BATTERY, 10);
    lv_obj_t *row = uiButton(s, 22, 160, 236, 40, lowPowerTap, NULL);
    uiLabel(row, &font_label, COL_TEXT, 0, 7, "LOW POWER MODE");
    bSwitch = uiSwitch(row, 190, 6, settings.lowPower);
    uiLabel(s, &font_small, COL_GREY, 22, 206, "DIM, NO ANIMATION, 12 H SYNC");
    return batteryRefresh;
}

// ----- Level ---------------------------------------------------------------------------
static lv_obj_t   *lBubble, *lX, *lY, *lState;
static lv_timer_t *appTimer = nullptr;   // fast refresh for Level, Timer, Stopwatch
static float       fx = 0, fy = 0;
static uint32_t    lastLog = 0;

static void levelTick(lv_timer_t *) {
    float ax, ay, az;
    if (!imuReadAccel(ax, ay, az)) return;
    if (millis() - lastLog > 1000) {   // Phase 0: confirms the face-up axis for raise to wake
        lastLog = millis();
        Serial.printf("imu x=%.2f y=%.2f z=%.2f\n", ax, ay, az);
    }
    az *= IMU_FACE_SIGN;   // face up = +1 g, so flat is 0° and not a flip between +-180°
    float x = atan2f(LEVEL_SWAP_XY ? ay : ax, az) * 57.2958f;
    float y = atan2f(LEVEL_SWAP_XY ? ax : ay, az) * 57.2958f;
    if (LEVEL_INV_X) x = -x;
    if (LEVEL_INV_Y) y = -y;
    fx += 0.3f * (x - fx);
    fy += 0.3f * (y - fy);

    int dx = (int)LV_CLAMP(-66, -fx * 4, 66);
    int dy = (int)LV_CLAMP(-66, fy * 4, 66);
    lv_obj_set_pos(lBubble, 106 - 14 + dx, 128 - 14 + dy);
    bool level = fabsf(fx) < 1 && fabsf(fy) < 1;
    lv_obj_set_style_bg_color(lBubble, lv_color_hex(level ? COL_ACTIVITY : COL_LEVEL), 0);

    char b[12];
    snprintf(b, sizeof(b), "%.1f°", fx); uiSetText(lX, b);
    snprintf(b, sizeof(b), "%.1f°", fy); uiSetText(lY, b);
    uiSetText(lState, level ? "LEVEL" : "TILTED");
    lv_obj_set_style_text_color(lState, lv_color_hex(level ? COL_ACTIVITY : COL_GREY), 0);
}

static void levelRefresh() {}

RefreshFn buildLevel(lv_obj_t *s) {
    uiHeader(s, "LEVEL", COL_LEVEL);
    uiCircle(s, 106, 128, 168, COL_LINE, 2);
    uiRect(s, 106, 52, 1, 152, COL_FILL, 0);
    uiRect(s, 30, 128, 152, 1, COL_FILL, 0);
    uiCircle(s, 106, 128, 36, 0x636366, 2);
    lBubble = uiCircle(s, 106, 128, 28, COL_LEVEL, 0);
    uiLabel(s, &font_small, COL_GREY, 202, 58, "X");
    lX = uiLabel(s, &font_label, COL_TEXT, 202, 76, "0.0°");
    uiLabel(s, &font_small, COL_GREY, 202, 108, "Y");
    lY = uiLabel(s, &font_label, COL_TEXT, 202, 126, "0.0°");
    lState = uiLabel(s, &font_small, COL_GREY, 202, 162, "TILTED");
    fx = fy = 0;
    appTimer = lv_timer_create(levelTick, 40, NULL);   // 25 Hz
    return levelRefresh;
}

void appsLeaving() {
    if (appTimer) { lv_timer_delete(appTimer); appTimer = nullptr; }
    wAge = nullptr;
}

// Rounded button with a centered label; returns the label.
static lv_obj_t *pill(lv_obj_t *s, int x, int w, const char *text, uint32_t color, lv_event_cb_t cb, void *data) {
    lv_obj_t *b = uiButton(s, x, 172, w, 52, cb, data);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_t *l = uiLabel(b, &font_label, color == COL_FILL ? COL_TEXT : 0x000000, 0, 0, text);
    lv_obj_center(l);
    return l;
}

static lv_obj_t *bigTime(lv_obj_t *s, int y) {
    lv_obj_t *l = uiLabel(s, &font_time, COL_TEXT, 0, y, "00:00");
    lv_obj_set_width(l, SCREEN_W);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

// ----- Timer ---------------------------------------------------------------------------
static const uint8_t TIMER_MIN[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60, 90 };
static lv_obj_t *tTime, *tBar;
static CdState   tBuilt;

static void timerTick(lv_timer_t *) {
    if (countdown.state != tBuilt) { uiGoLater(SCR_TIMER, LV_SCR_LOAD_ANIM_NONE); return; }   // new buttons
    uint32_t left = cdLeftMs(), secs = (left + 999) / 1000;   // round up: 05:00 at the start, 00:01 at the end
    char b[8];
    snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    uiSetText(tTime, b);
    lv_obj_set_width(tBar, LV_MAX(4, (int)(236ULL * left / (countdown.setMin * 60000UL))));
}

static void timerStep(lv_event_t *e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int n = sizeof(TIMER_MIN), i = 0;
    while (i < n - 1 && TIMER_MIN[i] < countdown.setMin) i++;
    i = LV_CLAMP(0, i + dir, n - 1);
    countdown.setMin = TIMER_MIN[i];
    timerTick(nullptr);
}

static void timerGo(lv_event_t *)     { cdStartPause(); timerTick(nullptr); }
static void timerCancel(lv_event_t *) { cdCancel(); timerTick(nullptr); }
static void timerRefresh() {}

RefreshFn buildTimer(lv_obj_t *s) {
    uiHeader(s, "TIMER", COL_TIMER);
    tBuilt = countdown.state;
    tTime = bigTime(s, 40);
    uiRect(s, 22, 128, 236, 8, COL_FILL, 4);
    tBar = uiRect(s, 22, 128, 236, 8, COL_TIMER, 4);
    const char *note = tBuilt == CD_PAUSED ? "PAUSED" : tBuilt == CD_DONE ? "TIME'S UP" : "";
    lv_obj_t *l = uiLabel(s, &font_small, tBuilt == CD_DONE ? COL_TIMER : COL_GREY, 0, 0, note);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 144);
    switch (tBuilt) {
        case CD_IDLE:
            pill(s, 20, 60, "-", COL_FILL, timerStep, (void *)(intptr_t)-1);
            pill(s, 90, 100, "START", COL_TIMER, timerGo, NULL);
            pill(s, 200, 60, "+", COL_FILL, timerStep, (void *)(intptr_t)1);
            break;
        case CD_DONE:
            lv_obj_set_style_text_color(tTime, lv_color_hex(COL_TIMER), 0);
            pill(s, 80, 120, "OK", COL_TIMER, timerCancel, NULL);
            break;
        default:
            pill(s, 20, 115, "CANCEL", COL_FILL, timerCancel, NULL);
            pill(s, 145, 115, tBuilt == CD_RUNNING ? "PAUSE" : "RESUME", COL_TIMER, timerGo, NULL);
            break;
    }
    timerTick(nullptr);
    appTimer = lv_timer_create(timerTick, 200, NULL);
    return timerRefresh;
}

// ----- Stopwatch -----------------------------------------------------------------------
static lv_obj_t *sTime, *sFrac, *sLap, *sLeft, *sRight;

static void stopwatchTick(lv_timer_t *) {
    uint32_t ms = swElapsedMs(), secs = ms / 1000, mins = secs / 60;
    char b[32];
    if (mins < 60) {   // MM:SS big, hundredths below
        snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)mins, (unsigned long)(secs % 60));
        uiSetText(sTime, b);
        snprintf(b, sizeof(b), ".%02lu", (unsigned long)(ms % 1000 / 10));
    } else {           // H:MM big, seconds below
        snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(mins / 60), (unsigned long)(mins % 60));
        uiSetText(sTime, b);
        snprintf(b, sizeof(b), ":%02lu", (unsigned long)(secs % 60));
    }
    uiSetText(sFrac, b);
    if (stopwatch.laps) {
        uint32_t l = stopwatch.lastLap;
        snprintf(b, sizeof(b), "LAP %u   %02lu:%02lu.%02lu", stopwatch.laps, (unsigned long)(l / 60000),
                 (unsigned long)(l / 1000 % 60), (unsigned long)(l % 1000 / 10));
    } else b[0] = 0;
    uiSetText(sLap, b);
    uiSetText(sLeft, stopwatch.running ? "LAP" : "RESET");
    uiSetText(sRight, stopwatch.running ? "STOP" : "START");
}

static void swLeftTap(lv_event_t *)  { swLapReset(); stopwatchTick(nullptr); }
static void swRightTap(lv_event_t *) { swStartStop(); stopwatchTick(nullptr); }
static void stopwatchRefresh() {}

RefreshFn buildStopwatch(lv_obj_t *s) {
    uiHeader(s, "STOPWATCH", COL_STOPWATCH);
    sTime = bigTime(s, 34);
    sFrac = uiLabel(s, &font_label, COL_STOPWATCH, 0, 0, "");
    lv_obj_align(sFrac, LV_ALIGN_TOP_MID, 0, 110);
    sLap = uiLabel(s, &font_small, COL_GREY, 0, 0, "");
    lv_obj_align(sLap, LV_ALIGN_TOP_MID, 0, 144);
    sLeft = pill(s, 20, 115, "", COL_FILL, swLeftTap, NULL);
    sRight = pill(s, 145, 115, "", COL_STOPWATCH, swRightTap, NULL);
    stopwatchTick(nullptr);
    appTimer = lv_timer_create(stopwatchTick, 50, NULL);   // 20 Hz for the hundredths
    return stopwatchRefresh;
}

// ----- Settings ---------------------------------------------------------------------------
enum RowId : uint8_t { R_BRIGHT, R_TIMEOUT, R_RAISE, R_H24, R_TZ, R_GOAL, R_WIFI, R_CITY, R_CLOCK, R_ABOUT, R_COUNT };
static const char *ROW_NAMES[R_COUNT] = {
    "BRIGHTNESS", "SCREEN TIMEOUT", "RAISE TO WAKE", "24-HOUR TIME", "TIME ZONE",
    "STEP GOAL", "WI-FI SETUP", "WEATHER CITY", "CLOCK CHIP", "ABOUT"
};
static lv_obj_t *rowValue[R_COUNT];
static lv_obj_t *rowSwitch[R_COUNT];

static void settingsRefresh() {
    char b[48];   // room for a 39-character city name plus " >"
    for (uint8_t i = 0; i < R_COUNT; i++) {
        if (rowSwitch[i]) {
            uiSetSwitch(rowSwitch[i], i == R_RAISE ? settings.raiseToWake : settings.h24);
            continue;
        }
        switch (i) {
            case R_BRIGHT:  snprintf(b, sizeof(b), "%u%% >", settings.brightness); break;
            case R_TIMEOUT: snprintf(b, sizeof(b), "%u S >", settings.timeoutS); break;
            case R_TZ:      snprintf(b, sizeof(b), "%s >", TZ_LIST[settings.tzIndex < TZ_COUNT ? settings.tzIndex : 0].label); break;
            case R_GOAL:    { char g[12]; fmtThousands(g, sizeof(g), settings.stepGoal); snprintf(b, sizeof(b), "%s >", g); } break;
            case R_WIFI:    strlcpy(b, netHasCredentials() ? "SAVED >" : "SET UP >", sizeof(b)); break;
            case R_CITY:    snprintf(b, sizeof(b), "%s >", settings.city[0] ? settings.city : "NOT SET"); break;
            case R_CLOCK:   strlcpy(b, clockRtcOk() ? "OK" : "NOT WORKING", sizeof(b)); break;
            default:        strlcpy(b, VERSION_TEXT " >", sizeof(b)); break;
        }
        uiSetText(rowValue[i], b);
    }
}

static void rowTap(lv_event_t *e) {
    switch ((RowId)(uintptr_t)lv_event_get_user_data(e)) {
        case R_BRIGHT:  settingsNextBrightness(); displaySetBacklight(uiBrightnessPercent()); break;
        case R_TIMEOUT: settingsNextTimeout(); break;
        case R_RAISE:   settings.raiseToWake = !settings.raiseToWake; settingsSave(); break;
        case R_H24:     settings.h24 = !settings.h24; settingsSave(); break;
        case R_TZ:      clockNextTz(); break;
        case R_GOAL:    settingsNextStepGoal(); break;
        case R_WIFI:
        case R_CITY:    uiGoLater(SCR_WIFI, LV_SCR_LOAD_ANIM_FADE_IN); uiRequest(REQ_PORTAL_START); return;
        case R_ABOUT:   uiOpenApp(SCR_ABOUT, SCR_SETTINGS); return;
        default:        return;
    }
    settingsRefresh();
}

RefreshFn buildSettings(lv_obj_t *s) {
    uiHeader(s, "SETTINGS", COL_GREY);
    lv_obj_t *list = lv_obj_create(s);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_size(list, SCREEN_W, SCREEN_H - 36);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a sideways swipe still goes back

    for (uint8_t i = 0; i < R_COUNT; i++) {
        lv_obj_t *row = uiButton(list, 0, 0, SCREEN_W, 52, rowTap, (void *)(uintptr_t)i);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
        uiLabel(row, &font_label, COL_TEXT, 24, 13, ROW_NAMES[i]);
        rowValue[i] = nullptr;
        rowSwitch[i] = nullptr;
        if (i == R_RAISE || i == R_H24) {
            rowSwitch[i] = uiSwitch(row, 210, 12, false);
        } else {
            rowValue[i] = rightLabel(row, &font_small, COL_GREY, 100, 18, 156);
            uiDots(rowValue[i], 156);
        }
    }
    return settingsRefresh;
}

// ----- About ------------------------------------------------------------------------------
static void aboutRefresh() {}

RefreshFn buildAbout(lv_obj_t *s) {
    uiHeader(s, "ABOUT", COL_GREY);
    lv_obj_t *list = lv_obj_create(s);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_size(list, SCREEN_W, SCREEN_H - 36);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);

    char chip[32], mem[32], bat[16], ram[16];
    snprintf(chip, sizeof(chip), "%s R%u %lu MHZ", ESP.getChipModel(), ESP.getChipRevision(),
             (unsigned long)ESP.getCpuFreqMHz());
    snprintf(mem, sizeof(mem), "%lu MB + %lu MB PSRAM", (unsigned long)(ESP.getFlashChipSize() >> 20),
             (unsigned long)(ESP.getPsramSize() >> 20));
    snprintf(bat, sizeof(bat), "%.2f V", battery.volts);
    snprintf(ram, sizeof(ram), "%lu KB", (unsigned long)(ESP.getFreeHeap() >> 10));
    const char *rows[][2] = {
        { "VERSION",   VERSION_TEXT },
        { "BUILT",     BUILD_STAMP },
        { "DEVELOPER", "SENJU" },
        { "BOARD",     "WAVESHARE 1.69\"" },
        { "CHIP",      chip },
        { "FLASH",     mem },
        { "SCREEN",    "ST7789 280X240" },
        { "TOUCH",     "CST816T" },
        { "MOTION",    "QMI8658" },
        { "CLOCK",     clockRtcOk() ? "PCF85063 OK" : "PCF85063 NOT WORKING" },
        { "BATTERY",   bat },
        { "FREE RAM",  ram },
    };
    for (auto &r : rows) {
        lv_obj_t *row = uiBox(list, 0, 0, SCREEN_W, 34);
        uiLabel(row, &font_small, COL_GREY, 24, 8, r[0]);
        lv_obj_t *v = rightLabel(row, &font_small, COL_TEXT, 96, 8, 160);
        uiDots(v, 160);
        uiSetText(v, r[1]);
    }
    return aboutRefresh;
}

// ----- Wi-Fi setup -------------------------------------------------------------------------
static lv_obj_t *wifiStatus;

// Rather than send you looking for a network that isn't there.
static void wifiRefresh() { uiSetText(wifiStatus, netPortalFailed() ? "NETWORK DID NOT START" : ""); }

RefreshFn buildWifi(lv_obj_t *s) {
    uiHeader(s, "WI-FI SETUP", COL_GREY);
    const struct { const lv_font_t *f; uint32_t c; int y; const char *t; } lines[] = {
        { &font_small, COL_GREY, 52,  "ON YOUR PHONE, JOIN THE NETWORK" },
        { &font_label, COL_TEXT, 76,  "HORODYNE-SETUP" },
        { &font_small, COL_GREY, 116, "THEN PICK YOUR WI-FI" },
        { &font_small, COL_GREY, 136, "AND TYPE YOUR CITY" },
    };
    for (auto &ln : lines) {
        lv_obj_t *l = uiLabel(s, ln.f, ln.c, 0, 0, ln.t);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, ln.y);
    }
    wifiStatus = uiLabel(s, &font_small, COL_WARN, 0, 0, "");
    lv_obj_align(wifiStatus, LV_ALIGN_TOP_MID, 0, 168);
    return wifiRefresh;
}
