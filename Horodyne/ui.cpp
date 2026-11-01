// Navigation, the clock face, Smart Stack, Control Center, app grid and overlays.
#include "ui_internal.h"
#include "svc_clock.h"
#include "svc_settings.h"
#include "svc_activity.h"
#include "svc_net.h"
#include "svc_github.h"
#include "hal_battery.h"
#include "hal_display.h"

static ScreenId  cur = SCR_FACE;
static ScreenId  backTarget = SCR_GRID;
static ScreenId  lastApp = SCR_FACE;      // SCR_FACE = no app used yet
uint8_t          uiStackIndex = 0;
static UiRequest pending = REQ_NONE;
static bool      syncing = false;
static RefreshFn curRefresh = nullptr;
static lv_obj_t *hdrTime = nullptr;
static lv_obj_t *sheet = nullptr;
static lv_obj_t *notice = nullptr;

static bool isApp(ScreenId id) { return id >= SCR_ACTIVITY && id <= SCR_ABOUT; }

ScreenId  uiCurrent() { return cur; }
void      uiRequest(UiRequest r) { pending = r; }
UiRequest uiTakeRequest() { UiRequest r = pending; pending = REQ_NONE; return r; }
bool      uiSyncing() { return syncing; }
bool      uiPowerSheetOpen() { return sheet != nullptr; }

uint8_t uiBrightnessPercent() {
    uint8_t b = settings.brightness;
    return (settings.lowPower && b > 40) ? 40 : b;
}

// ----- shared pieces ------------------------------------------------------------
static void onGesture(lv_event_t *e);

lv_obj_t *uiScreen() {
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_set_style_bg_color(s, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, onGesture, LV_EVENT_GESTURE, NULL);
    return s;
}

void uiHeader(lv_obj_t *scr, const char *title, uint32_t color) {
    lv_obj_t *t = uiLabel(scr, &font_small, color, 24, 12, title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, 170);
    char b[8];
    clockTimeText(b, sizeof(b));
    hdrTime = uiLabel(scr, &font_small, COL_TEXT, 0, 0, b);
    lv_obj_align(hdrTime, LV_ALIGN_TOP_RIGHT, -24, 12);
}

// ----- navigation ---------------------------------------------------------------
static RefreshFn buildFace(lv_obj_t *s);
static RefreshFn buildStack(lv_obj_t *s);
static RefreshFn buildControl(lv_obj_t *s);
static RefreshFn buildGrid(lv_obj_t *s);

static bool isGhSub(ScreenId id) { return id >= SCR_GH_PRS && id <= SCR_GH_COMMITS; }

void uiGo(ScreenId id, lv_screen_load_anim_t anim) {
    appsLeaving();
    hdrTime = nullptr;
    curRefresh = nullptr;
    lv_obj_t *s = uiScreen();
    cur = id;
    switch (id) {
        case SCR_FACE:     curRefresh = buildFace(s); break;
        case SCR_STACK:    curRefresh = buildStack(s); break;
        case SCR_CONTROL:  curRefresh = buildControl(s); break;
        case SCR_GRID:     curRefresh = buildGrid(s); break;
        case SCR_ACTIVITY: curRefresh = buildActivity(s); break;
        case SCR_WEATHER:  curRefresh = buildWeather(s); break;
        case SCR_BATTERY:  curRefresh = buildBattery(s); break;
        case SCR_LEVEL:    curRefresh = buildLevel(s); break;
        case SCR_TIMER:    curRefresh = buildTimer(s); break;
        case SCR_STOPWATCH: curRefresh = buildStopwatch(s); break;
        case SCR_GITHUB:   curRefresh = buildGithub(s); break;
        case SCR_GH_PRS:   curRefresh = buildGhPrs(s); break;
        case SCR_GH_PR:    curRefresh = buildGhPr(s); break;
        case SCR_GH_RUNS:  curRefresh = buildGhRuns(s); break;
        case SCR_GH_RUN:   curRefresh = buildGhRun(s); break;
        case SCR_GH_COMMITS: curRefresh = buildGhCommits(s); break;
        case SCR_SETTINGS: curRefresh = buildSettings(s); break;
        case SCR_ABOUT:    curRefresh = buildAbout(s); break;
        case SCR_WIFI:     curRefresh = buildWifi(s); break;
    }
    if (curRefresh) curRefresh();
    bool animate = anim != LV_SCR_LOAD_ANIM_NONE && !settings.lowPower;
    lv_screen_load_anim(s, animate ? anim : LV_SCR_LOAD_ANIM_NONE, animate ? 240 : 0, 0, true);
}

static ScreenId              laterId;
static lv_screen_load_anim_t laterAnim;
static bool                  laterPending = false;
static void laterCb(void *) { laterPending = false; uiGo(laterId, laterAnim); }

void uiGoLater(ScreenId id, lv_screen_load_anim_t anim) {
    laterId = id;
    laterAnim = anim;
    if (!laterPending) { laterPending = true; lv_async_call(laterCb, NULL); }
}

void uiOpenApp(ScreenId app, ScreenId backTo) {
    backTarget = backTo;
    lastApp = app;
    uiGoLater(app, LV_SCR_LOAD_ANIM_FADE_IN);
}

static void swipe(lv_dir_t d) {
    switch (cur) {
        case SCR_FACE:
            if (d == LV_DIR_TOP)         { uiStackIndex = 0; uiGoLater(SCR_STACK, LV_SCR_LOAD_ANIM_MOVE_TOP); }
            else if (d == LV_DIR_BOTTOM) uiGoLater(SCR_CONTROL, LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
            break;
        case SCR_STACK:
            if (d == LV_DIR_TOP && uiStackIndex < 3) { uiStackIndex++; uiGoLater(SCR_STACK, LV_SCR_LOAD_ANIM_MOVE_TOP); }
            else if (d == LV_DIR_BOTTOM) {
                if (uiStackIndex > 0) { uiStackIndex--; uiGoLater(SCR_STACK, LV_SCR_LOAD_ANIM_MOVE_BOTTOM); }
                else uiGoLater(SCR_FACE, LV_SCR_LOAD_ANIM_MOVE_BOTTOM);
            }
            break;
        case SCR_CONTROL:
            if (d == LV_DIR_TOP) uiGoLater(SCR_FACE, LV_SCR_LOAD_ANIM_MOVE_TOP);
            break;
        case SCR_GRID:
            if (d == LV_DIR_RIGHT) uiGoLater(SCR_FACE, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
            break;
        case SCR_WIFI:
            break;
        default:
            if (isApp(cur) && d == LV_DIR_RIGHT) uiGoLater(backTarget, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
            else if (isGhSub(cur) && d == LV_DIR_RIGHT) {
                ScreenId up = cur == SCR_GH_PR ? SCR_GH_PRS : cur == SCR_GH_RUN ? SCR_GH_RUNS : SCR_GITHUB;
                uiGoLater(up, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
            }
            break;
    }
}

static void onGesture(lv_event_t *) {
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_dir_t d = lv_indev_get_gesture_dir(in);
    lv_indev_wait_release(in);   // the swipe shouldn't also count as a tap
    swipe(d);
}

// ----- clock face ------------------------------------------------------------------
// "Modular": battery pill on top, a ring of ticks for today's steps (orange up to the goal),
// the hour in orange over the minutes in Valorax, weather pill at the bottom.
static const int DIAL = 112, TICKS = 40;   // 9 degrees per tick
static lv_obj_t *fHour, *fMin, *fAmPm, *fSteps, *fBatt, *fWx, *fCiGlyph, *fCi, *fPrGlyph, *fPrs, *fRefresh;
static lv_obj_t *fDone;   // orange ticks: as many as the share of the goal walked

static void openFromFace(lv_event_t *e) {
    uiOpenApp((ScreenId)(uintptr_t)lv_event_get_user_data(e), SCR_FACE);
}

// A pill: rounded outline (or fill) around one centered label; the box around it is the tap area.
static lv_obj_t *facePill(lv_obj_t *s, int y, ScreenId app) {
    lv_obj_t *box = uiButton(s, 40, y, 200, 32, openFromFace, (void *)(uintptr_t)app);
    lv_obj_t *l = uiLabel(box, &font_sm_small, COL_TEXT, 0, 0, "");
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(l, 1, 0);
    lv_obj_set_style_border_color(l, lv_color_hex(COL_LINE), 0);
    lv_obj_set_style_pad_hor(l, 12, 0);
    lv_obj_set_style_pad_ver(l, 5, 0);
    lv_obj_center(l);
    return l;
}

static lv_obj_t *faceTicks(lv_obj_t *p, uint32_t color) {
    lv_obj_t *sc = lv_scale_create(p);
    lv_obj_remove_flag(sc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sc, DIAL, DIAL);
    lv_obj_center(sc);
    lv_scale_set_mode(sc, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_rotation(sc, 270);   // start at 12 o'clock
    lv_scale_set_major_tick_every(sc, 0);
    lv_scale_set_label_show(sc, false);
    lv_obj_set_style_arc_opa(sc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_line_color(sc, lv_color_hex(color), LV_PART_ITEMS);
    lv_obj_set_style_line_width(sc, 2, LV_PART_ITEMS);
    lv_obj_set_style_length(sc, 9, LV_PART_ITEMS);
    return sc;
}

static void faceRefreshTap(lv_event_t *) {
    if (syncing) return;
    if (!settings.ghToken[0]) { uiOpenApp(SCR_GITHUB, SCR_FACE); return; }   // shows how to connect
    uiRequest(REQ_GH_SYNC);
}

static void faceRefresh() {
    char b[32];
    bool valid = clockValid();
    struct tm t = {};
    if (valid) { time_t now = time(nullptr); localtime_r(&now, &t); }
    int hour12 = t.tm_hour % 12 ? t.tm_hour % 12 : 12;
    if (valid) snprintf(b, sizeof(b), "%02d", settings.h24 ? t.tm_hour : hour12); else strlcpy(b, "--", sizeof(b));
    uiSetText(fHour, b);
    uiSetText(fAmPm, !valid || settings.h24 ? "" : t.tm_hour < 12 ? "AM" : "PM");
    if (valid) snprintf(b, sizeof(b), "%02d", t.tm_min); else strlcpy(b, "--", sizeof(b));
    uiSetText(fMin, b);

    fmtThousands(b, sizeof(b), activity.steps);
    uiSetText(fSteps, b);
    uint32_t goal = settings.stepGoal ? settings.stepGoal : 1;
    int done = (int)LV_MIN(activity.steps * TICKS / goal, (uint32_t)TICKS);
    if (activity.steps && done < 2) done = 2;   // show the walk has started (and keep the scale >= 2 ticks)
    if (done) {
        lv_obj_remove_flag(fDone, LV_OBJ_FLAG_HIDDEN);
        lv_scale_set_total_tick_count(fDone, done);
        lv_scale_set_angle_range(fDone, (done - 1) * 360 / TICKS);
    } else {
        lv_obj_add_flag(fDone, LV_OBJ_FLAG_HIDDEN);
    }

    if (weather.valid) snprintf(b, sizeof(b), "%d°C", weather.temp); else strlcpy(b, "--°C", sizeof(b));
    uiSetText(fWx, b);

    // GitHub half of the pill: grey once the data is over an hour old, dashes before the first fetch.
    const GhRun *run = gh.fetchedAt ? ghHeadRun(gh) : nullptr;
    bool old = gh.fetchedAt && time(nullptr) - gh.fetchedAt > 3600;
    Glyph g = G_WAIT;
    uint32_t c = COL_GREY;
    if (run) { ghStateGlyph(run->state, g, c); if (old) c = COL_GREY; }
    uiSetGlyph(fCiGlyph, g, c);
    if (run && run->jobCount) snprintf(b, sizeof(b), "%u/%u", run->jobsDone(), run->jobCount);
    else                      strlcpy(b, "--", sizeof(b));
    uiSetText(fCi, b);
    lv_obj_set_style_text_color(fCi, lv_color_hex(old ? COL_GREY : COL_TEXT), 0);
    uiSetGlyph(fPrGlyph, G_PR, old || !gh.fetchedAt ? COL_GREY : COL_ACTIVITY);
    if (gh.fetchedAt) snprintf(b, sizeof(b), "%u", gh.openCount); else strlcpy(b, "-", sizeof(b));
    uiSetText(fPrs, b);
    lv_obj_set_style_text_color(fPrs, lv_color_hex(old ? COL_GREY : COL_TEXT), 0);
    uiSetGlyph(fRefresh, syncing ? G_RUN : G_REFRESH, syncing ? COL_BATTERY : COL_SOFT);

    // Charging: an orange pill like the reference; low: red text.
    if (!battery.present)      strlcpy(b, "USB", sizeof(b));
    else if (battery.charging) snprintf(b, sizeof(b), "+%u%%", battery.percent);
    else                       snprintf(b, sizeof(b), "%u%%", battery.percent);
    uiSetText(fBatt, b);
    bool chg = battery.charging;
    bool low = battery.present && !chg && battery.percent <= BAT_WARN_PCT;
    lv_obj_set_style_bg_color(fBatt, lv_color_hex(COL_FACE), 0);
    lv_obj_set_style_bg_opa(fBatt, chg ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fBatt, chg ? 0 : 1, 0);
    lv_obj_set_style_text_color(fBatt, lv_color_hex(chg ? COL_BG : low ? COL_WARN : COL_TEXT), 0);
}

static RefreshFn buildFace(lv_obj_t *s) {
    fBatt = facePill(s, 4, SCR_BATTERY);

    // Steps dial: grey ticks all round, orange ones over them for the share of the goal.
    lv_obj_t *dial = uiButton(s, 16, 60, DIAL, DIAL, openFromFace, (void *)(uintptr_t)SCR_ACTIVITY);
    lv_obj_t *all = faceTicks(dial, COL_LINE);
    lv_scale_set_total_tick_count(all, TICKS);
    lv_scale_set_angle_range(all, 360 - 360 / TICKS);
    fDone = faceTicks(dial, COL_FACE);
    fSteps = uiLabel(dial, &font_vx_num, COL_TEXT, 0, 0, "");
    lv_obj_align(fSteps, LV_ALIGN_CENTER, 0, -6);
    lv_obj_t *u = uiLabel(dial, &font_sm_small, COL_GREY, 0, 0, "steps");
    lv_obj_align(u, LV_ALIGN_CENTER, 0, 14);

    // Time: hour over minutes, right-aligned in the right half.
    fHour = uiLabel(s, &font_vx_time, COL_FACE, 0, 0, "--");
    lv_obj_align(fHour, LV_ALIGN_TOP_RIGHT, -18, 62);
    fAmPm = uiLabel(s, &font_vx_num, COL_FACE, 0, 0, "");   // above the hour, same right edge
    lv_obj_align(fAmPm, LV_ALIGN_TOP_RIGHT, -18, 46);
    fMin = uiLabel(s, &font_vx_time, COL_TEXT, 0, 0, "--");
    lv_obj_align(fMin, LV_ALIGN_TOP_RIGHT, -18, 124);

    // Bottom pill: weather | CI + open PRs, each half its own tap target, refresh button beside it.
    uiRect(s, 36, 204, 196, 28, COL_LINE, 14);
    uiRect(s, 37, 205, 194, 26, COL_BG, 13);
    uiRect(s, 98, 210, 1, 16, COL_LINE, 0);
    lv_obj_t *wx = uiButton(s, 36, 200, 62, 36, openFromFace, (void *)(uintptr_t)SCR_WEATHER);
    fWx = uiLabel(wx, &font_sm_small, COL_TEXT, 14, 9, "");
    lv_obj_t *ghb = uiButton(s, 99, 200, 133, 36, openFromFace, (void *)(uintptr_t)SCR_GITHUB);
    fCiGlyph = uiGlyph(ghb, G_WAIT, 9, 10, 16, COL_GREY);
    fCi = uiLabel(ghb, &font_sm_small, COL_TEXT, 28, 9, "");
    fPrGlyph = uiGlyph(ghb, G_PR, 69, 11, 14, COL_GREY);
    fPrs = uiLabel(ghb, &font_sm_small, COL_TEXT, 87, 9, "");
    lv_obj_t *rb = uiButton(s, 233, 196, 44, 44, faceRefreshTap, NULL);
    uiGlyph(rb, G_TILE, 5, 5, 34, COL_LINE);
    fRefresh = uiGlyph(rb, G_REFRESH, 13, 13, 18, COL_SOFT);
    return faceRefresh;
}

// ----- Smart Stack -------------------------------------------------------------
static const ScreenId STACK_APPS[4] = { SCR_ACTIVITY, SCR_WEATHER, SCR_BATTERY, SCR_GITHUB };

static void openFromStack(lv_event_t *) { uiOpenApp(STACK_APPS[uiStackIndex], SCR_STACK); }

static lv_obj_t *cardText(lv_obj_t *card, int x, int y, const lv_font_t *f, uint32_t c, const char *t, int w) {
    lv_obj_t *l = uiLabel(card, f, c, x, y, t);
    if (w) { lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_width(l, w); }
    return l;
}

static void fillCard(lv_obj_t *card, uint8_t idx, bool full) {
    char b[32];
    if (idx == 0) {
        if (full) uiRing(card, 16, 26, 96, 13, COL_ACTIVITY, settings.stepGoal ? (int)(activity.steps * 1000UL / settings.stepGoal) : 0);
        cardText(card, full ? 128 : 20, 28, &font_small, COL_ACTIVITY, "ACTIVITY", 0);
        if (!full) return;
        fmtThousands(b, sizeof(b), activity.steps);
        cardText(card, 128, 50, &font_value, COL_TEXT, b, 0);
        char g[12]; fmtThousands(g, sizeof(g), settings.stepGoal);
        snprintf(b, sizeof(b), "OF %s STEPS", g);
        cardText(card, 128, 96, &font_small, COL_GREY, b, 104);
    } else if (idx == 1) {
        if (full) {
            lv_obj_t *ib = uiBox(card, 18, 42, 64, 64);
            iconWeather(ib, 0, 0, 64, weather.valid ? weather.code : 0, weather.valid ? weather.isDay : true, COL_WEATHER, COL_CARD);
        }
        cardText(card, full ? 100 : 20, 28, &font_small, COL_WEATHER, settings.city[0] ? settings.city : "WEATHER", 128);
        if (!full) return;
        if (weather.valid) snprintf(b, sizeof(b), "%d°", weather.temp); else strlcpy(b, "--°", sizeof(b));
        cardText(card, 100, 50, &font_value, COL_TEXT, b, 0);
        if (weather.valid) snprintf(b, sizeof(b), "%s  H%d L%d", weatherCondition(weather.code), weather.hi, weather.lo);
        else               strlcpy(b, settings.city[0] ? "NO DATA YET" : "SET A CITY", sizeof(b));
        cardText(card, 100, 96, &font_small, COL_GREY, b, 128);
    } else if (idx == 3) {
        const GhRun *run = gh.fetchedAt ? ghHeadRun(gh) : nullptr;
        Glyph g = G_WAIT; uint32_t c = COL_GREY;
        if (run) ghStateGlyph(run->state, g, c);
        if (full) {
            uiRing(card, 16, 26, 96, 13, c, run && run->jobCount ? run->jobsDone() * 1000 / run->jobCount : 0);
            if (run && run->jobCount) snprintf(b, sizeof(b), "%u/%u", run->jobsDone(), run->jobCount);
            else                      strlcpy(b, "--", sizeof(b));
            lv_obj_t *v = cardText(card, 16, 62, &font_label, COL_TEXT, b, 0);
            lv_obj_set_width(v, 96);
            lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
        }
        cardText(card, full ? 128 : 20, 28, &font_small, COL_TEXT, "GITHUB", 0);
        if (!full) return;
        if (gh.fetchedAt) snprintf(b, sizeof(b), "%u", gh.openCount); else strlcpy(b, "-", sizeof(b));
        cardText(card, 128, 50, &font_value, COL_TEXT, b, 0);
        cardText(card, 154, 72, &font_small, COL_GREY, "OPEN PRS", 0);
        strlcpy(b, !run ? "NO CI DATA" : run->state == GH_RUN ? "CI RUNNING" : run->state == GH_FAIL ? "CI FAILED" : "CI PASSED", sizeof(b));
        cardText(card, 128, 104, &font_small, c, b, 100);
    } else {
        cardText(card, 20, 28, &font_small, COL_BATTERY, "BATTERY", 0);
        if (!full) return;
        if (battery.present) snprintf(b, sizeof(b), "%u%%", battery.percent); else strlcpy(b, "USB", sizeof(b));
        cardText(card, 20, 50, &font_value, COL_TEXT, b, 0);
        if (battery.charging)          strlcpy(b, "CHARGING", sizeof(b));
        else if (battery.daysLeft > 0) snprintf(b, sizeof(b), "ABOUT %d DAYS LEFT", (int)roundf(battery.daysLeft));
        else                           strlcpy(b, "ESTIMATING TIME LEFT", sizeof(b));
        cardText(card, 20, 96, &font_small, COL_GREY, b, 200);
        uiRect(card, 20, 122, 140, 10, 0x3A3315, 5);
        uiRect(card, 20, 122, LV_MAX(2, 140 * (battery.present ? battery.percent : 100) / 100), 10, COL_BATTERY, 5);
    }
}

static void stackRefresh() {}

static RefreshFn buildStack(lv_obj_t *s) {
    uiHeader(s, "", COL_TEXT);
    lv_obj_t *card = uiButton(s, 18, 40, 238, 148, openFromStack, NULL);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 26, 0);
    fillCard(card, uiStackIndex, true);

    if (uiStackIndex < 3) {
        lv_obj_t *peek = uiRect(s, 30, 200, 214, 148, COL_CARD, 26);
        lv_obj_set_style_opa(peek, LV_OPA_50, 0);
        fillCard(peek, uiStackIndex + 1, false);
    }
    for (uint8_t i = 0; i < 4; i++) {
        bool on = i == uiStackIndex;
        uiRect(s, 266, 108 + i * 10 - (on ? 0 : 0), 5, on ? 12 : 5, on ? COL_TEXT : 0x48484A, 3);
    }
    return stackRefresh;
}

// ----- Control Center ------------------------------------------------------------------
enum CcId : uint8_t { CC_BRIGHT, CC_SYNC, CC_TIMEOUT, CC_RAISE, CC_LOW, CC_WIFI };

static void ccTap(lv_event_t *e) {
    switch ((CcId)(uintptr_t)lv_event_get_user_data(e)) {
        case CC_BRIGHT:  settingsNextBrightness(); displaySetBacklight(uiBrightnessPercent()); break;
        case CC_SYNC:    if (!syncing && netHasCredentials()) { syncing = true; uiRequest(REQ_SYNC); } break;
        case CC_TIMEOUT: settingsNextTimeout(); break;
        case CC_RAISE:   settings.raiseToWake = !settings.raiseToWake; settingsSave(); break;
        case CC_LOW:     settings.lowPower = !settings.lowPower; settingsSave(); displaySetBacklight(uiBrightnessPercent()); break;
        case CC_WIFI:    uiGoLater(SCR_WIFI, LV_SCR_LOAD_ANIM_FADE_IN); uiRequest(REQ_PORTAL_START); return;
    }
    uiGoLater(SCR_CONTROL, LV_SCR_LOAD_ANIM_NONE);   // redraw with the new state
}

static void ccButton(lv_obj_t *s, int cx, int top, CcId id, const char *label, bool on, uint32_t onColor) {
    lv_obj_t *b = uiButton(s, cx - 40, top, 80, 90, ccTap, (void *)(uintptr_t)id);
    uiCircle(b, 40, 31, 62, on ? onColor : COL_FILL, 0);
    uint32_t ic = on ? 0x000000 : COL_TEXT;
    lv_obj_t *ib = uiBox(b, 27, 18, 26, 26);
    switch (id) {
        case CC_BRIGHT:  iconSun(ib, 0, 0, 26, ic); break;
        case CC_SYNC:    iconSync(ib, 0, 0, 26, ic); break;
        case CC_TIMEOUT: iconTimer(ib, 0, 0, 26, ic); break;
        case CC_RAISE:   iconRaise26(ib, 0, 0, ic); break;
        case CC_LOW:     iconBolt26(ib, 0, 0, ic); break;
        case CC_WIFI:    iconWifi(ib, 0, 0, 26, ic); break;
    }
    lv_obj_t *l = uiLabel(b, &font_small, COL_SOFT, 0, 0, label);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 68);
}

static void controlRefresh() {}

static RefreshFn buildControl(lv_obj_t *s) {
    char b[16];
    snprintf(b, sizeof(b), "%u%%", settings.brightness);
    ccButton(s, 54, 24, CC_BRIGHT, b, true, COL_TEXT);
    ccButton(s, 140, 24, CC_SYNC, syncing ? "SYNCING" : (netHasCredentials() ? "SYNC" : "NO WI-FI"), syncing, COL_WEATHER);
    snprintf(b, sizeof(b), "%u S", settings.timeoutS);
    ccButton(s, 226, 24, CC_TIMEOUT, b, false, 0);
    ccButton(s, 54, 120, CC_RAISE, "RAISE", settings.raiseToWake, COL_ACTIVITY);
    ccButton(s, 140, 120, CC_LOW, "LOW PWR", settings.lowPower, COL_BATTERY);
    if (netLastSyncMs() == 0) strlcpy(b, netHasCredentials() ? "WI-FI" : "SET UP", sizeof(b));
    else                      strlcpy(b, netLastSyncOk() ? "WI-FI OK" : "WI-FI ERR", sizeof(b));
    ccButton(s, 226, 120, CC_WIFI, b, false, 0);
    uiRect(s, 122, 226, 36, 4, 0x48484A, 2);   // grab handle: swipe up to close
    return controlRefresh;
}

// ----- app grid -----------------------------------------------------------------------
static void openFromGrid(lv_event_t *e) {
    uiOpenApp((ScreenId)(uintptr_t)lv_event_get_user_data(e), SCR_GRID);
}

static void gridIcon(lv_obj_t *s, int cx, int top, ScreenId app, uint32_t color, const char *name) {
    lv_obj_t *b = uiButton(s, cx - 35, top, 70, 88, openFromGrid, (void *)(uintptr_t)app);
    lv_obj_add_flag(b, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   // "STOPWATCH" is wider than its button
    uiCircle(b, 35, 28, 56, color, 0);
    lv_obj_t *ib = uiBox(b, 19, 12, 32, 32);
    switch (app) {
        case SCR_ACTIVITY: uiRing(ib, -2, -2, 36, 6, 0x000000, 780); break;
        case SCR_WEATHER:  iconSun(ib, 0, 0, 32, 0x000000); break;
        case SCR_BATTERY:  iconBattery(ib, 0, 0, 32, 0x000000, battery.present ? battery.percent : 100); break;
        case SCR_LEVEL:    iconLevel(ib, 0, 0, 32, 0x000000); break;
        case SCR_TIMER:    uiRing(ib, 0, 0, 32, 5, 0x000000, 700); uiRect(ib, 15, 5, 3, 12, 0x000000, 1); break;
        case SCR_STOPWATCH: iconTimer(ib, 0, 0, 32, 0x000000); break;
        case SCR_GITHUB:   uiGlyph(ib, G_GH, 2, 2, 28, 0x000000); break;
        default:           iconGear(ib, 0, 0, 32, 0x000000); break;
    }
    lv_obj_t *l = uiLabel(b, &font_small, COL_TEXT, 0, 0, name);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 62);
}

static void gridRefresh() {}

static RefreshFn buildGrid(lv_obj_t *s) {
    gridIcon(s, 35, 24, SCR_ACTIVITY, COL_ACTIVITY, "ACTIVITY");
    gridIcon(s, 105, 24, SCR_WEATHER, COL_WEATHER, "WEATHER");
    gridIcon(s, 175, 24, SCR_BATTERY, COL_BATTERY, "BATTERY");
    gridIcon(s, 245, 24, SCR_LEVEL, COL_LEVEL, "LEVEL");
    gridIcon(s, 35, 128, SCR_TIMER, COL_TIMER, "TIMER");
    gridIcon(s, 105, 128, SCR_STOPWATCH, COL_STOPWATCH, "STOPWATCH");
    gridIcon(s, 175, 128, SCR_SETTINGS, COL_GREY, "SETTINGS");
    gridIcon(s, 245, 128, SCR_GITHUB, COL_TEXT, "GITHUB");
    return gridRefresh;
}

// ----- overlays -----------------------------------------------------------------------
static void sheetReleased(lv_event_t *e) {
    lv_obj_t *sl = (lv_obj_t *)lv_event_get_target(e);
    if (lv_slider_get_value(sl) >= 90) uiRequest(REQ_POWEROFF);
    else lv_slider_set_value(sl, 0, LV_ANIM_ON);
}

static void showSheet() {
    if (sheet) return;
    sheet = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(sheet);
    lv_obj_set_size(sheet, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(sheet, 225, 0);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);   // nothing underneath can be pressed
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sl = lv_slider_create(sheet);
    lv_obj_set_size(sl, 226, 56);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, -12);
    lv_slider_set_range(sl, 0, 100);
    lv_obj_set_style_bg_color(sl, lv_color_hex(COL_FILL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, 28, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(sl, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(COL_WARN), LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, -4, LV_PART_KNOB);
    lv_obj_add_event_cb(sl, sheetReleased, LV_EVENT_RELEASED, NULL);
    lv_obj_t *l = uiLabel(sl, &font_small, COL_SOFT, 0, 0, "SLIDE TO POWER OFF");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 36, 0);

    lv_obj_t *c = uiLabel(sheet, &font_small, COL_GREY, 0, 0, "PRESS PWR TO CANCEL");
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 40);
}

static void hideSheet() {
    if (sheet) { lv_obj_delete(sheet); sheet = nullptr; }
}

static lv_timer_t *noticeT = nullptr;

static void removeNotice(bool fromItsOwnEvent) {
    if (noticeT) { lv_timer_delete(noticeT); noticeT = nullptr; }
    if (!notice) return;
    if (fromItsOwnEvent) lv_obj_delete_async(notice);
    else                 lv_obj_delete(notice);
    notice = nullptr;
}

static void noticeTap(lv_event_t *) { removeNotice(true); }

static void noticeTimer(lv_timer_t *) {
    noticeT = nullptr;   // a one-shot timer deletes itself after this callback
    removeNotice(false);
}

static void showNotice(const char *big, const char *line1, const char *line2, uint32_t color, uint32_t ms) {
    removeNotice(false);
    notice = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(notice);
    lv_obj_set_size(notice, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(notice, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(notice, LV_OPA_COVER, 0);
    lv_obj_add_flag(notice, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(notice, noticeTap, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ib = uiBox(notice, 0, 0, 44, 44);
    lv_obj_align(ib, LV_ALIGN_TOP_MID, 0, 28);
    iconBattery(ib, 0, 0, 44, color, 10);
    lv_obj_t *l = uiLabel(notice, &font_hero, color, 0, 0, big);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 76);
    l = uiLabel(notice, &font_label, COL_TEXT, 0, 0, line1);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 146);
    l = uiLabel(notice, &font_small, COL_GREY, 0, 0, line2);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 178);

    noticeT = lv_timer_create(noticeTimer, ms, NULL);
    lv_timer_set_repeat_count(noticeT, 1);
}

void uiShowLowBattery() {
    char b[8];
    snprintf(b, sizeof(b), "%u%%", battery.percent);
    showNotice(b, "LOW POWER MODE IS ON", "CHARGE SOON", COL_WARN, 4000);
}

void uiShowChargeMe() {
    char b[8];
    snprintf(b, sizeof(b), "%u%%", battery.percent);
    showNotice(b, "CHARGE ME", "SWITCHING OFF", COL_WARN, 60000);
}

// ----- events from the main loop -------------------------------------------------------
void uiOnPwr(PwrEvent e) {
    if (cur == SCR_WIFI) {
        if (e == PWR_CLICK) { uiRequest(REQ_PORTAL_CANCEL); uiGo(SCR_SETTINGS, LV_SCR_LOAD_ANIM_FADE_IN); }
        return;
    }
    switch (e) {
        case PWR_CLICK:
            if (sheet)  { hideSheet(); return; }
            if (notice) { removeNotice(false); return; }
            uiGo(cur == SCR_FACE ? SCR_GRID : SCR_FACE, LV_SCR_LOAD_ANIM_FADE_IN);
            break;
        case PWR_DOUBLE:
            if (isApp(lastApp)) { backTarget = SCR_FACE; uiGo(lastApp, LV_SCR_LOAD_ANIM_FADE_IN); }
            break;
        case PWR_LONG:
            showSheet();
            break;
        default: break;
    }
}

void uiOnWake(bool backToFace) {
    if (backToFace && cur != SCR_FACE && cur != SCR_WIFI) {
        hideSheet();
        uiGo(SCR_FACE, LV_SCR_LOAD_ANIM_NONE);
    } else {
        uiRefresh();
    }
}

void uiRefresh() {
    if (cur == SCR_LEVEL || cur == SCR_WIFI) { if (curRefresh) curRefresh(); return; }
    uiGo(cur, LV_SCR_LOAD_ANIM_NONE);
}

void uiTick() {
    if (hdrTime) {
        char b[8];
        clockTimeText(b, sizeof(b));
        uiSetText(hdrTime, b);
    }
    if (curRefresh) curRefresh();
}

void uiSetSyncing(bool on) { syncing = on; }

void uiPortalDone(bool ok) {
    uiGo(ok ? SCR_FACE : SCR_SETTINGS, LV_SCR_LOAD_ANIM_FADE_IN);
}

void uiSplash() {
    lv_obj_t *s = uiScreen();
    lv_obj_t *l = uiLabel(s, &font_title, COL_TEXT, 0, 0, "HoroDyne");
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -12);
    l = uiLabel(s, &font_small, COL_GREY, 0, 0, VERSION_TEXT);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 30);
    l = uiLabel(s, &font_small, COL_SOFT, 0, 0, "SENJU");
    lv_obj_align(l, LV_ALIGN_BOTTOM_RIGHT, -22, -16);
    lv_screen_load(s);   // the first uiGo() deletes it
}

void uiBegin() {
    uiGo(SCR_FACE, LV_SCR_LOAD_ANIM_NONE);
}
