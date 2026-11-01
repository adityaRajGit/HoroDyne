/* ==========================================================================
   HORODYNE -- minimalist watch for the Waveshare ESP32-S3-Touch-LCD-1.69
   Landscape 280 x 240, Unica One, LVGL 9 on LovyanGFX.

   BEFORE THE FIRST UPLOAD (Arduino Cloud editor)
   1. Libraries > Import: horodyne_lvconf.zip  (LVGL's settings file)
   2. Libraries (tested): lvgl 9.6.0, LovyanGFX 1.2.32, WiFiManager 2.0.17 (tzapu),
      ArduinoJson 7.4.3, SensorLib 0.5.0
   3. Board: ESP32S3 Dev Module, ESP32 core 3.3.x.  Flash Size: 16MB (128Mb),
      PSRAM: OPI PSRAM, USB CDC On Boot: Enabled,
      Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
   4. Check the battery plug polarity before connecting the cell.

   SCREEN STATES
   Active -> Dim (after the timeout) -> Sleep (2 s later, ESP32 in light sleep)
   Sleep wakes on: raise wrist (IMU), touch, PWR, or the 6-hourly sync timer.
   ========================================================================== */

#include <Wire.h>
#include <lv_conf.h>   // from the horodyne_lvconf library; must come before lvgl.h
#include <lvgl.h>
#include "config.h"
#include "hal_display.h"
#include "hal_power.h"
#include "hal_battery.h"
#include "hal_imu.h"
#include "svc_settings.h"
#include "svc_clock.h"
#include "svc_net.h"
#include "svc_github.h"
#include "svc_activity.h"
#include "svc_timer.h"
#include "ui.h"
#include "build_info.h"
#include <Preferences.h>

enum PowerState : uint8_t { ST_ACTIVE, ST_DIM };

static PowerState state = ST_ACTIVE;
static uint32_t   lastInputMs = 0;
static uint32_t   lastTickMs = 0;
static uint32_t   lastBattMs = 0;
static uint32_t   raiseCooldownUntil = 0;
static bool       lowWarned = false;

// ----- helpers -------------------------------------------------------------------
static uint32_t screenTimeoutMs() {
    return uiCurrent() == SCR_LEVEL ? LEVEL_TIMEOUT_MS : settings.timeoutS * 1000UL;
}

static uint8_t dimPercent() {
    uint8_t b = uiBrightnessPercent() * 3 / 10;
    return b < 5 ? 5 : b;
}

static bool syncDue() {
    if (!netHasCredentials()) return false;
    uint32_t last = netLastSyncMs();
    if (last == 0) return true;
    uint32_t every = settings.lowPower ? SYNC_EVERY_LOW_MS : SYNC_EVERY_MS;
    if (!netLastSyncOk()) every = 3600000UL;   // retry hourly after a failure
    return millis() - last >= every;
}

// Milliseconds until the sleeping watch must wake by itself: next sync, the countdown
// reaching zero, or the next 2-hour step-history block (one of them is midnight).
static uint32_t msUntilTimerWake() {
    uint32_t best = 0;
    if (netHasCredentials()) {
        uint32_t last = netLastSyncMs();
        uint32_t every = settings.lowPower ? SYNC_EVERY_LOW_MS : SYNC_EVERY_MS;
        if (!netLastSyncOk()) every = 3600000UL;
        uint32_t since = millis() - last;
        best = (last == 0 || since >= every) ? 1000 : every - since;
    }
    uint32_t block = clockMsUntilMidnight();
    if (block) {
        block = block % (2 * 3600000UL) + 2000;   // just after the boundary, so steps land in the right block
        if (best == 0 || block < best) best = block;
    }
    if (countdown.state == CD_RUNNING) {
        uint32_t left = cdLeftMs() + 1;
        if (best == 0 || left < best) best = left;
    }
    // Steps: drain the IMU FIFO before it overflows (~4 s). ~20 ms awake per wake.
    // ponytail: wakes even when still (~0.2 mA); skip while the any-motion detector is quiet if it matters.
    if (best == 0 || STEP_POLL_MS < best) best = STEP_POLL_MS;
    return best;
}

static void runSync() {
    uiSetSyncing(true);
    lv_refr_now(NULL);
    netSync();
    uiSetSyncing(false);
    activityUpdate();
}

// Low battery: warning + Low Power Mode at 10 %, safe shutdown at 3 %.
static void checkBattery(bool screenOn) {
    static uint8_t lowCount = 0;
    // Low Power Mode switched itself on at 10 %; like a phone, it switches off again at 80 % on the charger.
    if (battery.charging && settings.lowPower && battery.percent >= 80) { settings.lowPower = false; settingsSave(); }
    if (!battery.present || battery.charging) { lowWarned = false; lowCount = 0; return; }
    // Three low readings in a row (~30 s awake) before switching off: one reading taken
    // under load, right after the cable comes out, can sag far below the real charge.
    lowCount = battery.percent <= BAT_SHUTDOWN_PCT ? lowCount + 1 : 0;
    if (lowCount >= 3) {
        if (!screenOn) displayWake();
        uiShowChargeMe();
        lv_refr_now(NULL);
        displaySetBacklight(dimPercent());
        delay(3000);
        displaySleep();
        stepsSave(true);
        powerOff();
    }
    if (battery.percent <= BAT_LOWPOWER_PCT && !lowWarned) {
        lowWarned = true;
        if (!settings.lowPower) { settings.lowPower = true; settingsSave(); }
        if (screenOn) { uiShowLowBattery(); displaySetBacklight(uiBrightnessPercent()); }
    }
    if (battery.percent > BAT_LOWPOWER_PCT + 5) lowWarned = false;
}

// ----- sleep ------------------------------------------------------------------------
static void sleepUntilWake() {
    uint32_t sleptAt = millis();
    displaySleep();

    while (true) {
        if (settings.raiseToWake) imuArmWakeOnMotion();   // clears a latched motion event
        uint32_t timerMs = msUntilTimerWake();
        bool imuWake = settings.raiseToWake && (int32_t)(millis() - raiseCooldownUntil) >= 0;
        if (settings.raiseToWake && !imuWake) {
            uint32_t left = raiseCooldownUntil - millis();
            if (timerMs == 0 || left < timerMs) timerMs = left;
        }
        WakeReason why = powerLightSleep((uint64_t)timerMs * 1000ULL, imuWake);

        if (why == WAKE_TIMER) {
            if (countdown.state == CD_RUNNING && cdLeftMs() == 0) break;   // timer done: wake to ring
            if (syncDue()) runSync();
            activityUpdate();
            stepsSave(false);
            batteryUpdate(true);
            checkBattery(false);
            continue;   // back to sleep; the screen stays off
        }
        if (why == WAKE_IMU) {
            bool up = imuFacingUp();
            Serial.printf("wake: motion, %s\n", up ? "facing up" : "not facing up, back to sleep");
            if (up) break;
            raiseCooldownUntil = millis() + RAISE_COOLDOWN_MS;   // walking, not looking
            continue;
        }
        break;          // touch, PWR, or anything else wakes the screen
    }

    imuDisarmWakeOnMotion();
    batteryUpdate(true);          // resting reading, before the backlight draws current
    clockCorrectFromRtc();
    activityUpdate();
    displayIgnoreTouchUntilRelease();
    displayWake();
    uiOnWake(millis() - sleptAt > BACK_TO_FACE_MS);
    lv_timer_handler();
    lv_refr_now(NULL);
    displaySetBacklight(uiBrightnessPercent());
    checkBattery(true);
    state = ST_ACTIVE;
    lastInputMs = millis();
}

// ----- steps survive a reboot ---------------------------------------------------------
// ponytail: a crash loses up to STEP_SAVE_MS of steps; saving more often only wears the flash.
static const uint32_t STEP_SAVE_MS = 600000UL;

static void stepsSave(bool force) {
    static uint32_t lastSavedSteps = 0, lastSaveMs = 0;
    if (activity.steps == lastSavedSteps || (!force && millis() - lastSaveMs < STEP_SAVE_MS)) return;
    int day = clockDayOfYear();
    if (day < 0) return;
    Preferences p;
    p.begin("steps", false);
    p.putInt("day", day);
    p.putBytes("act", &activity, sizeof(activity));
    p.end();
    lastSavedSteps = activity.steps;
    lastSaveMs = millis();
}

static void stepsRestore() {
    int day = clockDayOfYear();
    if (day < 0) return;
    Preferences p;
    p.begin("steps", true);
    if (p.getInt("day", -1) == day) p.getBytes("act", &activity, sizeof(activity));
    p.end();
}

// Over USB: "gh <login> <token>" stores the GitHub details in NVS, "ghsync" fetches now.
// The token is never printed, and lives in NVS rather than in the firmware image.
void serialCommand(const char *line) {
    char user[40], token[128];
    if (!strcmp(line, "ghsync")) { uiRequest(REQ_GH_SYNC); return; }
    if (!strcmp(line, "s")) {
        Serial.printf("steps: %lu today, %lu reads, %lu samples, %lu empty, %lu peaks, biggest swing %.3f g, %u FIFO restarts\n",
                      (unsigned long)activity.steps, (unsigned long)activityStats.updates, (unsigned long)activityStats.samples,
                      (unsigned long)activityStats.emptyReads, (unsigned long)activityStats.peaks, activityStats.maxSwing, imuRestarts);
        return;
    }
    if (sscanf(line, "gh %39s %127s", user, token) == 2 && ghValidLogin(user)) {
        strlcpy(settings.ghUser, user, sizeof(settings.ghUser));
        strlcpy(settings.ghToken, token, sizeof(settings.ghToken));
        settingsSave();
        gh.error = GH_OK;
        Serial.printf("github: saved login %s, token %u chars\n", user, (unsigned)strlen(token));
        uiRefresh();
    }
}

// ----- Arduino entry points ------------------------------------------------------------
void setup() {
    powerLatch();                 // first: keeps the board on when PWR is released
    Serial.begin(115200);
    // On a computer's USB with no serial monitor open nobody reads the log: each print would then
    // wait up to 2 s (20 x 100 ms in HWCDC) and freeze the UI. Drop log lines instead of waiting.
    Serial.setTxTimeoutMs(0);
    Wire.setBufferSize(256);      // SensorLib reads the IMU FIFO 240 bytes at a time; the default 128 jams it
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    settingsLoad();
    batteryBegin();
    batteryUpdate(true);
    Serial.printf("horodyne: v%s.%d built %s\n", HORODYNE_VERSION, BUILD_NUMBER, BUILD_STAMP);
    displayBegin();
    uiSplash();                   // covers the IMU setup and the 1 s RTC check below
    lv_refr_now(NULL);
    displaySetBacklight(uiBrightnessPercent());
    uint32_t splashAt = millis();

    if (!imuBegin()) Serial.println("imu: QMI8658 not found");
    clockBegin();
    stepsRestore();
    activityUpdate();
    while (millis() - splashAt < 1500) delay(10);

    uiBegin();
    lv_timer_handler();
    lv_refr_now(NULL);

    if (netHasCredentials()) { runSync(); uiRefresh(); }
    lastInputMs = millis();
}

void loop() {
    uint32_t now = millis();

    // Input: touch (seen by the display driver) and the PWR key.
    uint32_t touched = displayLastTouchMs();
    if (touched && (int32_t)(touched - lastInputMs) > 0) lastInputMs = touched;
    PwrEvent pe = powerPoll();
    if (pe != PWR_NONE && cdRinging()) { cdCancel(); pe = PWR_NONE; lastInputMs = now; }   // PWR silences the alarm
    if (pe != PWR_NONE) {
        lastInputMs = now;
        if (state == ST_DIM && pe == PWR_CLICK) pe = PWR_NONE;   // the press only brightens the screen
        if (pe != PWR_NONE) uiOnPwr(pe);
    }

    // Work the UI asked for.
    switch (uiTakeRequest()) {
        case REQ_SYNC:          runSync(); uiRefresh(); lastInputMs = millis(); break;
        case REQ_GH_SYNC:
            uiSetSyncing(true);
            uiRefresh();
            lv_refr_now(NULL);
            netGithubSync();
            uiSetSyncing(false);
            activityUpdate();    // the fetch can outlast the IMU FIFO
            uiRefresh();
            lastInputMs = millis();
            break;
        case REQ_PORTAL_START:  netPortalStart(); break;
        case REQ_PORTAL_CANCEL: netPortalStop(); break;
        case REQ_POWEROFF:      displaySleep(); stepsSave(true); powerOff(); break;
        default: break;
    }
    if (uiCurrent() == SCR_WIFI) {
        int r = netPortalProcess();
        if (r != 0) uiPortalDone(r > 0);
        lastInputMs = now;   // keep the screen on while setting up
    }

    // Countdown reached zero: show it and beep-beep once a second until dismissed or TIMER_RING_MS.
    if (cdCheckDone()) { uiOpenApp(SCR_TIMER, SCR_FACE); lastInputMs = now; }
    uint32_t ph = (now - countdown.doneAt) % 1000;
    buzzerSet(cdRinging() && (ph < 120 || (ph >= 240 && ph < 360)));

    // Once a second: time and steps on screen. Every 10 s: battery.
    if (now - lastTickMs >= 1000) {
        lastTickMs = now;
        if (now - lastBattMs >= 10000) {   // often enough to see the cable go in or out
            lastBattMs = now;
            batteryUpdate(false);
            checkBattery(true);
        }
        activityUpdate();   // drains ~1 s of IMU samples
        stepsSave(false);
        batterySerialPoll();
        uiTick();
    }

    lv_timer_handler();
    // Lag probe: any pass of the loop this slow is a visible stutter; say where it went.
    uint32_t took = millis() - now;
    if (took > 50) Serial.printf("lag: loop %lu ms on screen %d\n", (unsigned long)took, (int)uiCurrent());

    // Screen timeout.
    bool keepOn = uiPowerSheetOpen() || uiCurrent() == SCR_WIFI || cdRinging();
    uint32_t idle = now - lastInputMs;
    if (keepOn) idle = 0;
    if (state == ST_ACTIVE && idle > screenTimeoutMs()) {
        state = ST_DIM;
        displaySetBacklight(dimPercent());
    } else if (state == ST_DIM && idle <= screenTimeoutMs()) {
        state = ST_ACTIVE;
        displaySetBacklight(uiBrightnessPercent());
    } else if (state == ST_DIM && idle > screenTimeoutMs() + DIM_MS) {
        sleepUntilWake();
    }

    delay(5);
}
