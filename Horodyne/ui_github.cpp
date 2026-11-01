// The GitHub app. Data comes from svc_github; the Figma board "Horodyne — GitHub" is the reference.
#include "ui_internal.h"
#include "svc_github.h"
#include "svc_settings.h"
#include "svc_clock.h"

// ----- glyphs ----------------------------------------------------------------------------
// Pairs of 0-24 coordinates; PEN starts a new stroke, END closes the list.
#define PEN -1
#define END -2
static const int8_t GL_PR[]      = { 3,3,9,3,9,9,3,9,3,3,PEN,15,15,21,15,21,21,15,21,15,15,PEN,6,9,6,21,PEN,12,6,16,6,18,8,18,15,END };
static const int8_t GL_MERGE[]   = { 3,3,9,3,9,9,3,9,3,3,PEN,15,15,21,15,21,21,15,21,15,15,PEN,6,9,6,15,9,18,15,18,END };
static const int8_t GL_COMMIT[]  = { 12,8,16,12,12,16,8,12,12,8,PEN,2,12,8,12,PEN,16,12,22,12,END };
static const int8_t GL_CHECK[]   = { 3,12,9,18,21,6,END };
static const int8_t GL_X[]       = { 4,4,10,10,PEN,14,14,20,20,PEN,20,4,14,10,PEN,10,14,4,20,END };
static const int8_t GL_RUN[]     = { 8,3,16,3,21,8,21,13,PEN,16,21,8,21,3,16,3,11,END };
static const int8_t GL_WAIT[]    = { 12,5,19,12,12,19,5,12,12,5,END };
static const int8_t GL_EYE[]     = { 2,12,8,6,16,6,22,12,16,18,8,18,2,12,PEN,10,10,14,10,14,14,10,14,10,10,END };
static const int8_t GL_GH[]      = { 9,22,9,17,8,15,4,13,4,3,9,6,15,6,20,3,20,13,16,15,15,17,15,22,PEN,9,18,6,18,3,15,END };
static const int8_t GL_WIFI[]    = { 2,9,12,3,22,9,PEN,6,13,12,9,18,13,PEN,10,17,12,16,14,17,PEN,11,20,13,20,END };
static const int8_t GL_REFRESH[] = { 4,11,4,7,7,4,18,4,21,7,PEN,21,3,21,8,16,8,PEN,20,13,20,17,17,20,6,20,3,17,PEN,3,21,3,16,8,16,END };
static const int8_t GL_TILE[]    = { 7,1,17,1,23,7,23,17,17,23,7,23,1,17,1,7,7,1,END };
static const int8_t *const GLYPHS[] = { GL_PR, GL_MERGE, GL_COMMIT, GL_CHECK, GL_X, GL_RUN, GL_WAIT, GL_EYE, GL_GH, GL_WIFI, GL_REFRESH, GL_TILE };

static void glyphDraw(lv_event_t *e) {
    lv_obj_t *o = (lv_obj_t *)lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int size = lv_area_get_width(&a);
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_obj_get_style_line_color(o, LV_PART_MAIN);
    d.width = 2;
    const int8_t *p = GLYPHS[(uintptr_t)lv_obj_get_user_data(o)];
    bool haveLast = false;
    float lastX = 0, lastY = 0;
    for (; *p != END; ) {
        if (*p == PEN) { haveLast = false; p++; continue; }
        float x = a.x1 + p[0] * size / 24.0f, y = a.y1 + p[1] * size / 24.0f;
        p += 2;
        if (haveLast) {
            d.p1.x = lastX; d.p1.y = lastY; d.p2.x = x; d.p2.y = y;
            lv_draw_line(lv_event_get_layer(e), &d);
        }
        lastX = x; lastY = y; haveLast = true;
    }
}

lv_obj_t *uiGlyph(lv_obj_t *parent, Glyph g, int x, int y, int size, uint32_t color) {
    lv_obj_t *o = uiBox(parent, x, y, size, size);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o, (void *)(uintptr_t)g);
    lv_obj_set_style_line_color(o, lv_color_hex(color), 0);
    lv_obj_add_event_cb(o, glyphDraw, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void uiSetGlyph(lv_obj_t *o, Glyph g, uint32_t color) {
    if ((uintptr_t)lv_obj_get_user_data(o) == g && lv_color_to_u32(lv_obj_get_style_line_color(o, LV_PART_MAIN)) == lv_color_to_u32(lv_color_hex(color))) return;   // called every second
    lv_obj_set_user_data(o, (void *)(uintptr_t)g);
    lv_obj_set_style_line_color(o, lv_color_hex(color), 0);
    lv_obj_invalidate(o);
}

void ghStateGlyph(uint8_t state, Glyph &g, uint32_t &c) {
    switch (state) {
        case GH_PASS: g = G_CHECK; c = COL_ACTIVITY; break;
        case GH_RUN:  g = G_RUN;   c = COL_BATTERY; break;
        case GH_FAIL: g = G_X;     c = COL_WARN; break;
        default:      g = G_WAIT;  c = COL_GREY; break;
    }
}

// ----- shared pieces -----------------------------------------------------------------------
static uint8_t tab = 0, prIdx = 0, runIdx = 0;   // open/merged, and which row was tapped
static lv_obj_t *mainAge = nullptr;

static void goTo(lv_event_t *e) { uiGoLater((ScreenId)(uintptr_t)lv_event_get_user_data(e), LV_SCR_LOAD_ANIM_FADE_IN); }

static void nothing() {}

static void centered(lv_obj_t *s, const lv_font_t *f, uint32_t c, int y, const char *t) {
    lv_obj_t *l = uiLabel(s, f, c, 30, y, t);
    lv_obj_set_width(l, 220);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

static void ageNow(char *b, size_t n, time_t when) {
    char a[8];
    ghAge(a, sizeof(a), when, time(nullptr));
    if (strcmp(a, "NOW") == 0) strlcpy(b, a, n); else snprintf(b, n, "%s AGO", a);
}

static void headerAge(lv_obj_t *s, int x) {
    if (uiSyncing()) { uiLabel(s, &font_small, COL_GREY, x, 12, "SYNCING"); return; }
    if (!gh.fetchedAt) return;
    char b[12];
    ageNow(b, sizeof(b), gh.fetchedAt);
    uiLabel(s, &font_small, COL_GREY, x, 12, b);
}

static lv_obj_t *scroller(lv_obj_t *s, int y, int h) {
    lv_obj_t *b = uiBox(s, 0, y, 280, h);
    lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(b, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_OFF);
    return b;
}

static lv_obj_t *dotLabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y, int w, const char *t) {
    lv_obj_t *l = uiLabel(p, f, c, x, y, t);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w);
    return l;
}

static const int ROW_H = 58;

static lv_obj_t *row(lv_obj_t *list, int y, Glyph g, uint32_t c, const char *title, const char *meta,
                     lv_event_cb_t cb, void *data, int titleW = 184) {
    lv_obj_t *r = uiButton(list, 0, y, 280, ROW_H, cb, data);
    uiGlyph(r, g, 24, 17, 20, c);
    dotLabel(r, &font_label, COL_TEXT, 52, 6, titleW, title);
    dotLabel(r, &font_small, COL_GREY, 52, 34, titleW, meta);
    uiRect(r, 24, ROW_H - 1, 232, 1, COL_FILL, 0);
    return r;
}

// ----- setup and error screens ------------------------------------------------------------
static void openSetup(lv_event_t *) {
    uiGoLater(SCR_WIFI, LV_SCR_LOAD_ANIM_FADE_IN);
    uiRequest(REQ_PORTAL_START);
}

static RefreshFn buildConnect(lv_obj_t *s) {
    uiHeader(s, "CONNECT GITHUB", COL_TEXT);
    uiGlyph(s, G_GH, 120, 36, 40, COL_TEXT);
    centered(s, &font_small, COL_GREY, 84, "ON YOUR PHONE, JOIN");
    centered(s, &font_label, COL_TEXT, 104, "HORODYNE-SETUP");
    centered(s, &font_small, COL_GREY, 132, "ADD YOUR USERNAME AND A READ-ONLY TOKEN");
    lv_obj_t *b = uiButton(s, 60, 180, 160, 44, openSetup, NULL);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_t *l = uiLabel(b, &font_small, COL_BG, 0, 0, "OPEN SETUP");
    lv_obj_center(l);
    return nothing;
}

static RefreshFn buildTrouble(lv_obj_t *s, const char *title, const char *why) {
    uiHeader(s, "GITHUB", COL_TEXT);
    uiGlyph(s, G_X, 122, 44, 36, COL_WARN);
    centered(s, &font_label, COL_TEXT, 90, title);
    centered(s, &font_small, COL_GREY, 118, why);
    lv_obj_t *b = uiButton(s, 50, 172, 180, 44, openSetup, NULL);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_t *l = uiLabel(b, &font_small, COL_BG, 0, 0, "OPEN SETUP");
    lv_obj_center(l);
    return nothing;
}

// ----- overview ----------------------------------------------------------------------------
static uint32_t lastAttemptMs = 0;
static const uint32_t REFETCH_MS = 60000;   // reopening the app within a minute reuses the data

static void askSync(lv_timer_t *) { uiRequest(REQ_GH_SYNC); }

static void retryTap(lv_event_t *) { if (!uiSyncing()) uiRequest(REQ_GH_SYNC); }

static void tile(lv_obj_t *s, int x, int y, const char *v, const char *name, uint32_t c, Glyph g, ScreenId to, bool dim) {
    lv_obj_t *b = uiButton(s, x, y, 80, 76, goTo, (void *)(uintptr_t)to);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 16, 0);
    uiLabel(b, &font_value, dim ? COL_SOFT : COL_TEXT, 10, 4, v);
    if (!dim) uiGlyph(b, g, 56, 8, 14, c);
    uiLabel(b, &font_small, dim ? COL_GREY : c, 10, 52, name);
}

static void openOpenPrs(lv_event_t *) { tab = 0; uiGoLater(SCR_GH_PRS, LV_SCR_LOAD_ANIM_FADE_IN); }
static void openMergedPrs(lv_event_t *) { tab = 1; uiGoLater(SCR_GH_PRS, LV_SCR_LOAD_ANIM_FADE_IN); }

RefreshFn buildGithub(lv_obj_t *s) {
    if (!settings.ghToken[0] || !settings.ghUser[0]) return buildConnect(s);
    if (!uiSyncing() && (!lastAttemptMs || millis() - lastAttemptMs > REFETCH_MS)) {
        lastAttemptMs = millis();
        lv_timer_t *t = lv_timer_create(askSync, 300, NULL);   // after the fade: the fetch blocks the UI
        lv_timer_set_repeat_count(t, 1);
    }
    if (gh.error == GH_BAD_TOKEN && !uiSyncing()) return buildTrouble(s, "TOKEN EXPIRED", "MAKE A NEW GITHUB TOKEN, THEN ADD IT IN SETUP");

    uiHeader(s, "GITHUB", COL_TEXT);
    headerAge(s, 80);
    char b[48];
    bool offline = !uiSyncing() && gh.error != GH_OK;

    if (!gh.fetchedAt) {                                // nothing yet: skeleton or a retry
        if (uiSyncing()) {
            uiRect(s, 12, 36, 256, 70, COL_CARD, 18);
            for (int i = 0; i < 3; i++) uiRect(s, 12 + i * 88, 114, 80, 76, COL_CARD, 16);
            uiGlyph(s, G_RUN, 124, 55, 32, COL_GREY);
        } else {
            uiGlyph(s, G_WIFI, 124, 60, 32, COL_LEVEL);
            centered(s, &font_label, COL_TEXT, 104, gh.error == GH_NO_WIFI ? "NO WI-FI" : gh.error == GH_RATE_LIMIT ? "TOO MANY REQUESTS" : "COULD NOT LOAD");
            lv_obj_t *r = uiButton(s, 60, 150, 160, 44, retryTap, NULL);
            lv_obj_set_style_bg_color(r, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(r, 22, 0);
            lv_obj_center(uiLabel(r, &font_small, COL_BG, 0, 0, "TRY AGAIN"));
        }
        return nothing;
    }

    int y = 36;
    if (offline) {                                      // old numbers, clearly marked
        lv_obj_t *r = uiButton(s, 12, y, 256, 34, retryTap, NULL);
        lv_obj_set_style_bg_color(r, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(r, 17, 0);
        uiGlyph(r, G_WIFI, 12, 8, 18, COL_LEVEL);
        uiLabel(r, &font_small, COL_LEVEL, 38, 8, gh.error == GH_NO_WIFI ? "NO WI-FI - TAP TO RETRY" : "SYNC FAILED - TAP TO RETRY");
        y += 42;
    }

    const GhRun *run = ghHeadRun(gh);
    Glyph g = G_WAIT; uint32_t c = COL_GREY;
    if (run) ghStateGlyph(run->state, g, c);
    lv_obj_t *ci = uiButton(s, 12, y, 256, offline ? 62 : 70, goTo, (void *)(uintptr_t)SCR_GH_RUNS);
    lv_obj_set_style_bg_color(ci, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(ci, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ci, 18, 0);
    uiGlyph(ci, g, 14, 16, 30, offline ? COL_GREY : c);
    uiLabel(ci, &font_label, offline ? COL_SOFT : COL_TEXT, 56, 6,
            !run ? "NO CI RUNS" : run->state == GH_RUN ? "CI RUNNING" : run->state == GH_FAIL ? "CI FAILED" : "CI PASSED");
    if (run) {
        snprintf(b, sizeof(b), "%s - %s - %u OF %u", run->repo, run->name, run->jobsDone(), run->jobCount);
        dotLabel(ci, &font_small, COL_GREY, 56, 32, 190, b);
        if (run->jobCount && !offline) {
            uiRect(ci, 56, 54, 186, 5, COL_FILL, 3);
            uiRect(ci, 56, 54, LV_MAX(4, 186 * run->jobsDone() / run->jobCount), 5, c, 3);
        }
    }
    y += offline ? 70 : 78;

    char v[8];
    snprintf(v, sizeof(v), "%u", gh.openCount);
    tile(s, 12, y, v, "OPEN", COL_ACTIVITY, G_PR, SCR_GH_PRS, offline);
    lv_obj_add_event_cb(lv_obj_get_child(s, -1), openOpenPrs, LV_EVENT_CLICKED, NULL);
    snprintf(v, sizeof(v), "%u", gh.mergedCount);
    tile(s, 100, y, v, "MERGED", COL_MERGED, G_MERGE, SCR_GH_PRS, offline);
    lv_obj_add_event_cb(lv_obj_get_child(s, -1), openMergedPrs, LV_EVENT_CLICKED, NULL);
    snprintf(v, sizeof(v), "%u", gh.reviewCount);
    tile(s, 188, y, v, "REVIEW", COL_LEVEL, G_EYE, SCR_GH_PRS, offline);

    if (!offline) {
        lv_obj_t *r = uiButton(s, 12, 194, 256, 36, goTo, (void *)(uintptr_t)SCR_GH_COMMITS);
        snprintf(b, sizeof(b), "%u COMMITS", gh.weekTotal);
        uiLabel(r, &font_small, COL_GREY, 12, 8, b);
        uint16_t mx = 1;
        for (int i = 7; i < 14; i++) if (gh.days[i] > mx) mx = gh.days[i];
        for (int i = 0; i < 7; i++) {
            int h = LV_MAX(2, gh.days[7 + i] * 18 / mx);
            uiRect(r, 120 + i * 16, 26 - h, 11, h, i == 6 ? COL_TEXT : 0x48484A, 2);
        }
    }
    return nothing;
}

// ----- pull requests ------------------------------------------------------------------------
static void setTab(lv_event_t *e) { tab = (uint8_t)(uintptr_t)lv_event_get_user_data(e); uiGoLater(SCR_GH_PRS, LV_SCR_LOAD_ANIM_NONE); }
static void openPr(lv_event_t *e) { prIdx = (uint8_t)(uintptr_t)lv_event_get_user_data(e); uiGoLater(SCR_GH_PR, LV_SCR_LOAD_ANIM_FADE_IN); }

static void tabButton(lv_obj_t *s, int x, const char *t, uint8_t id) {
    bool on = tab == id;
    lv_obj_t *b = uiButton(s, x, 36, 120, 32, setTab, (void *)(uintptr_t)id);
    lv_obj_set_style_bg_color(b, lv_color_hex(on ? COL_TEXT : COL_CARD), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_center(uiLabel(b, &font_small, on ? COL_BG : COL_GREY, 0, 0, t));
}

RefreshFn buildGhPrs(lv_obj_t *s) {
    uiHeader(s, "PULL REQUESTS", COL_TEXT);
    char b[48], a[8];
    snprintf(b, sizeof(b), "OPEN %u", gh.openCount);   tabButton(s, 16, b, 0);
    snprintf(b, sizeof(b), "MERGED %u", gh.mergedCount); tabButton(s, 144, b, 1);
    const GhPr *list = tab ? gh.merged : gh.open;
    uint8_t n = tab ? gh.mergedShown : gh.openShown;
    if (!n) {
        uiGlyph(s, tab ? G_MERGE : G_PR, 122, 88, 36, 0x48484A);
        centered(s, &font_label, COL_TEXT, 134, tab ? "NO MERGED PRS" : "NO OPEN PRS");
        centered(s, &font_small, COL_GREY, 162, "NEW PRS SHOW UP AFTER THE NEXT SYNC");
        return nothing;
    }
    lv_obj_t *l = scroller(s, 72, 168);
    for (uint8_t i = 0; i < n; i++) {
        const GhPr &p = list[i];
        ghAge(a, sizeof(a), p.when, time(nullptr));
        snprintf(b, sizeof(b), "#%lu - %s - %s", (unsigned long)p.number, p.repo, a);
        lv_obj_t *r = row(l, i * ROW_H, tab ? G_MERGE : G_PR, tab ? COL_MERGED : COL_ACTIVITY, p.title, b, openPr, (void *)(uintptr_t)i);
        if (!tab && p.ci != GH_NONE) {
            Glyph g; uint32_t c;
            ghStateGlyph(p.ci, g, c);
            uiGlyph(r, g, 238, 19, 18, c);
        }
    }
    return nothing;
}

RefreshFn buildGhPr(lv_obj_t *s) {
    const GhPr &p = (tab ? gh.merged : gh.open)[prIdx];
    char b[40];
    snprintf(b, sizeof(b), "#%lu", (unsigned long)p.number);
    uiHeader(s, b, COL_TEXT);
    lv_obj_t *t = uiLabel(s, &font_label, COL_TEXT, 24, 36, p.title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_size(t, 232, 54);

    uint32_t pill = tab ? COL_MERGED : COL_ACTIVITY;
    uiRect(s, 24, 96, tab ? 84 : 68, 26, pill, 13);
    uiGlyph(s, tab ? G_MERGE : G_PR, 32, 102, 14, 0);
    uiLabel(s, &font_small, COL_BG, 50, 101, tab ? "MERGED" : "OPEN");
    char a[8];
    ghAge(a, sizeof(a), p.when, time(nullptr));
    snprintf(b, sizeof(b), "%s - %s", p.repo, a);
    dotLabel(s, &font_small, COL_GREY, tab ? 118 : 102, 101, 136, b);

    snprintf(b, sizeof(b), "+%lu", (unsigned long)p.added);
    lv_obj_t *add = uiLabel(s, &font_label, COL_ACTIVITY, 24, 132, b);
    lv_obj_update_layout(add);
    int x = 24 + lv_obj_get_width(add) + 12;
    snprintf(b, sizeof(b), "-%lu", (unsigned long)p.removed);
    lv_obj_t *del = uiLabel(s, &font_label, COL_WARN, x, 132, b);
    lv_obj_update_layout(del);
    snprintf(b, sizeof(b), "%lu FILES", (unsigned long)p.files);
    uiLabel(s, &font_small, COL_GREY, x + lv_obj_get_width(del) + 14, 136, b);

    uiRect(s, 24, 164, 232, 1, COL_FILL, 0);
    Glyph g; uint32_t c;
    ghStateGlyph(p.ci, g, c);
    uiGlyph(s, g, 24, 174, 18, c);
    uiLabel(s, &font_small, COL_TEXT, 50, 174, "CHECKS");
    const char *state = p.ci == GH_PASS ? "PASSED" : p.ci == GH_RUN ? "RUNNING" : p.ci == GH_FAIL ? "FAILED" : "NONE";
    lv_obj_t *r = uiLabel(s, &font_small, COL_SOFT, 0, 0, state);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -24, 174);
    uiGlyph(s, G_EYE, 24, 202, 18, COL_LEVEL);
    uiLabel(s, &font_small, COL_TEXT, 50, 202, "REVIEWS");
    snprintf(b, sizeof(b), "%u OF %u", p.approvals, p.reviewers);
    r = uiLabel(s, &font_small, COL_SOFT, 0, 0, b);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -24, 202);
    return nothing;
}

// ----- pipelines ------------------------------------------------------------------------------
static void openRun(lv_event_t *e) { runIdx = (uint8_t)(uintptr_t)lv_event_get_user_data(e); uiGoLater(SCR_GH_RUN, LV_SCR_LOAD_ANIM_FADE_IN); }

RefreshFn buildGhRuns(lv_obj_t *s) {
    uiHeader(s, "PIPELINES", COL_TEXT);
    headerAge(s, 104);
    if (!gh.runCount) {
        uiGlyph(s, G_RUN, 122, 70, 36, 0x48484A);
        centered(s, &font_label, COL_TEXT, 116, "NO PIPELINE RUNS");
        centered(s, &font_small, COL_GREY, 144, "NO ACTIONS FOUND IN YOUR 3 LATEST REPOS");
        return nothing;
    }
    lv_obj_t *l = scroller(s, 34, 206);
    char b[56], a[8];
    for (uint8_t i = 0; i < gh.runCount; i++) {
        const GhRun &r = gh.runs[i];
        Glyph g; uint32_t c;
        ghStateGlyph(r.state, g, c);
        ghAge(a, sizeof(a), r.when, time(nullptr));
        if (r.state == GH_RUN) snprintf(b, sizeof(b), "%s - %u OF %u", r.repo, r.jobsDone(), r.jobCount);
        else                   snprintf(b, sizeof(b), "%s - %s - %s", r.repo, r.branch, a);
        lv_obj_t *o = row(l, i * ROW_H, g, c, r.name, b, openRun, (void *)(uintptr_t)i, 204);
        if (r.state == GH_RUN && r.jobCount) {
            uiRect(o, 52, 54, 204, 3, COL_FILL, 2);
            uiRect(o, 52, 54, LV_MAX(4, 204 * r.jobsDone() / r.jobCount), 3, c, 2);
        }
    }
    return nothing;
}

RefreshFn buildGhRun(lv_obj_t *s) {
    const GhRun &r = gh.runs[runIdx];
    char b[32];
    snprintf(b, sizeof(b), "RUN %lu", (unsigned long)r.number);
    uiHeader(s, b, COL_TEXT);
    headerAge(s, 96);
    dotLabel(s, &font_label, COL_TEXT, 24, 34, 232, r.name);
    int n = LV_MAX(1, r.jobCount), w = (232 - (n - 1) * 5) / n;
    for (int i = 0; i < r.jobCount; i++) {
        Glyph g; uint32_t c;
        ghStateGlyph(r.jobs[i].state, g, c);
        uiRect(s, 24 + i * (w + 5), 64, w, 6, r.jobs[i].state == GH_NONE ? COL_FILL : c, 3);
    }
    if (!r.jobCount) centered(s, &font_small, COL_GREY, 110, "NO JOB DETAILS");
    lv_obj_t *l = scroller(s, 76, 160);
    for (int i = 0; i < r.jobCount; i++) {
        const GhJob &j = r.jobs[i];
        Glyph g; uint32_t c;
        ghStateGlyph(j.state, g, c);
        int y = i * 29;
        uiGlyph(l, g, 24, y + 5, 18, j.state == GH_NONE ? 0x48484A : c);
        dotLabel(l, &font_label, j.state == GH_NONE ? COL_GREY : COL_TEXT, 50, y, 150, j.name);
        if (j.secs) snprintf(b, sizeof(b), "%d:%02d", j.secs / 60, j.secs % 60);
        else        strlcpy(b, j.state == GH_RUN ? "LIVE" : j.state == GH_NONE ? "WAIT" : "", sizeof(b));
        lv_obj_t *t = uiLabel(l, &font_small, j.state == GH_RUN ? COL_BATTERY : COL_GREY, 196, y + 4, b);
        lv_obj_set_width(t, 60);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_RIGHT, 0);
    }
    return nothing;
}

// ----- commits ------------------------------------------------------------------------------
RefreshFn buildGhCommits(lv_obj_t *s) {
    uiHeader(s, "COMMITS", COL_TEXT);
    headerAge(s, 88);
    char b[48], a[8];
    snprintf(b, sizeof(b), "%u", gh.weekTotal);
    uiLabel(s, &font_value, COL_TEXT, 24, 30, b);
    uiLabel(s, &font_small, COL_GREY, 84, 38, "THIS WEEK");
    snprintf(b, sizeof(b), "%u DAY STREAK", gh.streak);
    uiLabel(s, &font_small, COL_ACTIVITY, 84, 55, b);
    uint16_t mx = 1;
    for (int i = 0; i < 14; i++) if (gh.days[i] > mx) mx = gh.days[i];
    for (int i = 0; i < 14; i++) {
        lv_obj_t *c = uiRect(s, 24 + i * 169 / 10, 82, 14, 14, gh.days[i] ? COL_ACTIVITY : COL_FILL, 3);
        if (gh.days[i]) lv_obj_set_style_bg_opa(c, 90 + 165 * gh.days[i] / mx, 0);   // busier day, brighter
    }
    if (!gh.commitCount) { centered(s, &font_small, COL_GREY, 150, "NO RECENT COMMITS FOUND"); return nothing; }
    lv_obj_t *l = scroller(s, 102, 138);
    for (uint8_t i = 0; i < gh.commitCount; i++) {
        const GhCommit &c = gh.commits[i];
        lv_obj_t *r = uiBox(l, 0, i * ROW_H, 280, ROW_H);
        uiGlyph(r, G_COMMIT, 24, 17, 20, COL_GREY);
        dotLabel(r, &font_label, COL_TEXT, 52, 6, 204, c.message);
        uiLabel(r, &font_sm_small, COL_GREY, 52, 35, c.sha);
        ghAge(a, sizeof(a), c.when, time(nullptr));
        snprintf(b, sizeof(b), "- %s - %s", c.repo, a);
        dotLabel(r, &font_small, COL_GREY, 118, 34, 138, b);
        uiRect(r, 24, ROW_H - 1, 232, 1, COL_FILL, 0);
    }
    return nothing;
}
