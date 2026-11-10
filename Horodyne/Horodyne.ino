/* ==========================================================================
   HORODYNE -- minimalist watch for the Waveshare ESP32-S3-Touch-LCD-1.69
   Landscape 280 x 240, LVGL 9 on LovyanGFX.

   Build and flash with build.sh; libraries and board options are in the README.
   Check the battery plug polarity before connecting the cell.

   SCREEN STATES
   Active -> Dim (after the timeout) -> Sleep (2 s later, ESP32 in light sleep)
   Sleep wakes on: raise wrist (IMU), touch, PWR, or a timer (steps, sync, countdown).
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
#include <esp_task_wdt.h>

enum PowerState : uint8_t { ST_ACTIVE, ST_DIM };
enum NetJob : uint8_t { NET_NONE, NET_SYNC, NET_GITHUB };

static PowerState state = ST_ACTIVE;
static uint32_t   lastInputMs = 0;
static uint32_t   lastTickMs = 0;
static uint32_t   lastBattMs = 0;
static uint32_t   raiseCooldownUntil = 0;
static bool       lowWarned = false;
static uint32_t   wakesTimer = 0, wakesRaise = 0, wakesRejected = 0, wakesOther = 0;   // since boot, for "s"
static bool       lowPowerAuto = false;   // the battery turned Low Power Mode on, not the user
static TaskHandle_t  netTask = nullptr;
static volatile bool netBusy = false;     // the worker owns Wi-Fi until this clears
static NetJob        netJob = NET_NONE;   // running, or finished and not yet picked up by netFinish()

// ----- helpers -------------------------------------------------------------------
static uint32_t screenTimeoutMs() {
    return uiCurrent() == SCR_LEVEL ? LEVEL_TIMEOUT_MS : settings.timeoutS * 1000UL;
}

static uint8_t dimPercent() {
    uint8_t b = uiBrightnessPercent() * 3 / 10;
    return b < 5 ? 5 : b;
}

// Milliseconds until the next sync: 0 = due now, UINT32_MAX = none planned.
static uint32_t msUntilSync() {
    if (!batteryRadioOk() || !netHasCredentials()) return UINT32_MAX;   // battery first: the check starts Wi-Fi
    uint32_t last = netLastSyncMs();
    if (last == 0) return 0;
    uint32_t every = settings.lowPower ? SYNC_EVERY_LOW_MS : SYNC_EVERY_MS;
    if (!netLastSyncOk()) {                    // out of range: retry after 1, 2, then 4 hours
        uint8_t fails = netFailCount();
        uint32_t retry = 3600000UL << (fails > 3 ? 2 : fails ? fails - 1 : 0);
        if (retry < every) every = retry;
    }
    uint32_t since = millis() - last;
    return since >= every ? 0 : every - since;
}

static bool syncDue() { return msUntilSync() == 0; }

// Milliseconds until the sleeping watch must wake by itself: next sync, the countdown
// reaching zero, or the next 2-hour step-history block (one of them is midnight).
static uint32_t msUntilTimerWake() {
    uint32_t sync = msUntilSync();
    uint32_t best = sync == UINT32_MAX ? 0 : sync ? sync : 1000;
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

// Network jobs run here so the loop keeps serving the UI, PWR, the alarm and the IMU FIFO.
// The watchdog watches this task only during a job: idle, it blocks forever by design.
// ponytail: the UI can read weather mid-write for a moment; a wrong number lasts one refresh.
static void netWorker(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        esp_task_wdt_add(NULL);
        if (netJob == NET_GITHUB) netGithubSync(); else netSync();
        esp_task_wdt_delete(NULL);
        netBusy = false;
    }
}

static bool netStart(NetJob job) {
    if (netBusy || netJob != NET_NONE || !netTask) return false;
    netJob = job;
    netBusy = true;
    uiSetSyncing(true);
    xTaskNotifyGive(netTask);
    return true;
}

// Picks up a finished job on the loop task, the only one that may touch LVGL.
static void netFinish() {
    if (netBusy || netJob == NET_NONE) return;
    if (netJob == NET_GITHUB) ghApply();
    netJob = NET_NONE;
    uiSetSyncing(false);
    activityUpdate();
    uiRefresh();
}

// Low battery: warning + Low Power Mode at 10 %, safe shutdown at 3 %.
static void checkBattery(bool screenOn) {
    static uint8_t lowCount = 0;
    // Low Power Mode switched itself on at 10 %; like a phone, it switches off again at 80 % on the charger.
    // One the user turned on stays on.
    if (battery.charging && lowPowerAuto && battery.percent >= 80) { lowPowerAuto = false; settings.lowPower = false; settingsSave(); }
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
        if (!settings.lowPower) { settings.lowPower = true; lowPowerAuto = true; settingsSave(); }
        if (screenOn) { uiShowLowBattery(); displaySetBacklight(uiBrightnessPercent()); }
    }
    if (battery.percent > BAT_LOWPOWER_PCT + 5) lowWarned = false;
}

// ----- sleep ------------------------------------------------------------------------
static void sleepUntilWake() {
    uint32_t sleptAt = millis();
    displaySleep();

    while (true) {
        esp_task_wdt_reset();
        if (settings.raiseToWake) imuArmWakeOnMotion();   // clears a latched motion event
        uint32_t timerMs = msUntilTimerWake();
        bool imuWake = settings.raiseToWake && (int32_t)(millis() - raiseCooldownUntil) >= 0;
        if (settings.raiseToWake && !imuWake) {
            uint32_t left = raiseCooldownUntil - millis();
            if (timerMs == 0 || left < timerMs) timerMs = left;
        }
        // Wi-Fi can't run through light sleep: stay awake with the screen off until the job ends.
        bool busy = netBusy;
        if (busy && timerMs > 1000) timerMs = 1000;
        WakeReason why = powerLightSleep((uint64_t)timerMs * 1000ULL, imuWake, busy);

        if (why == WAKE_TIMER) {
            wakesTimer++;
            if (countdown.state == CD_RUNNING && cdLeftMs() == 0) break;   // timer done: wake to ring
            netFinish();
            if (syncDue()) netStart(NET_SYNC);
            activityUpdate();
            stepsSave(false);
            if (millis() - lastBattMs >= 10000) {
                lastBattMs = millis();
                batteryUpdate(true);
                checkBattery(false);
            }
            continue;   // back to sleep; the screen stays off
        }
        if (why == WAKE_IMU) {
            // Motion fires as the wrist starts turning, so give it time to finish.
            // ponytail: walking spends this awake on every motion wake (unmeasured); shorten RAISE_SETTLE_MS if it shows.
            uint32_t t0 = millis();
            bool up = imuFacingUp();
            while (!up && millis() - t0 < RAISE_SETTLE_MS) { delay(50); up = imuFacingUp(); }
            Serial.printf("wake: motion, %s\n", up ? "facing up" : "not facing up, back to sleep");
            if (up) { wakesRaise++; break; }
            wakesRejected++;
            raiseCooldownUntil = millis() + RAISE_COOLDOWN_MS;   // walking, not looking
            continue;
        }
        wakesOther++;
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
    stepsRestore();   // before any save: it would overwrite the record if the clock came late
    if (activity.steps == lastSavedSteps || (!force && millis() - lastSaveMs < STEP_SAVE_MS)) return;
    int day = clockDayNumber();
    if (day < 0) return;
    Preferences p;
    p.begin("steps", false);
    p.putInt("day", day);
    p.putBytes("act", &activity, sizeof(activity));
    p.end();
    lastSavedSteps = activity.steps;
    lastSaveMs = millis();
}

// Runs once the clock is known, which can be long after boot: adds the saved day to whatever
// was counted in the meantime.
static void stepsRestore() {
    static bool done = false;
    if (done) return;
    int day = clockDayNumber();
    if (day < 0) return;
    done = true;
    Activity saved;
    Preferences p;
    p.begin("steps", true);
    if (p.getInt("day", -1) == day && p.getBytes("act", &saved, sizeof(saved)) == sizeof(saved)) {
        activity.steps += saved.steps;
        for (int i = 0; i < 12; i++) {
            uint32_t b = activity.bins[i] + saved.bins[i];
            activity.bins[i] = b > 65535 ? 65535 : b;
        }
    }
    p.end();
}

// Over USB: "gh <login> <token>" stores the GitHub details in NVS, "ghsync" fetches now.
// The token is never printed, and lives in NVS rather than in the firmware image.
void serialCommand(const char *line) {
    char user[40], token[128];
    lastInputMs = millis();   // someone is at the serial port: don't let the screen time out under them
    if (!strcmp(line, "ghsync")) { uiRequest(REQ_GH_SYNC); return; }
    int q;
    if (!strcmp(line, "imu")) {
        imuDebugPrint();
        Serial.printf("imu: raise to wake is %s in Settings\n", settings.raiseToWake ? "on" : "off");
        return;
    }
    if (!strcmp(line, "wstat"))  { netDebugStatus(); return; }
    bool radioCmd = !strcmp(line, "wscan") || sscanf(line, "txp %d", &q) == 1;
    if (radioCmd && netBusy)     { Serial.println("net: a sync has Wi-Fi, try again in a moment"); return; }
    if (!strcmp(line, "wscan"))  { netDebugScan(); return; }
    if (sscanf(line, "txp %d", &q) == 1) { netDebugTxPower(q); return; }
    if (!strcmp(line, "s")) {
        Serial.printf("steps: %lu today, %lu reads, %lu samples, %lu empty, %lu peaks, biggest swing %.3f g, %u FIFO restarts\n",
                      (unsigned long)activity.steps, (unsigned long)activityStats.updates, (unsigned long)activityStats.samples,
                      (unsigned long)activityStats.emptyReads, (unsigned long)activityStats.peaks, activityStats.maxSwing, imuRestarts);
        Serial.printf("steps: up %lu s (reset reason %d), wakes: %lu timer, %lu raise, %lu motion rejected, %lu other\n",
                      (unsigned long)(millis() / 1000), (int)esp_reset_reason(), (unsigned long)wakesTimer,
                      (unsigned long)wakesRaise, (unsigned long)wakesRejected, (unsigned long)wakesOther);
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
    setCpuFrequencyMhz(160);      // 240 MHz by default; Wi-Fi and LVGL are fine at 160 and it draws less
    esp_task_wdt_config_t wdt = { .timeout_ms = WDT_TIMEOUT_S * 1000, .idle_core_mask = 1, .trigger_panic = true };
    if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
    esp_task_wdt_add(NULL);       // a hung loop reboots instead of freezing the watch
    Serial.begin(115200);
    // On a computer's USB with no serial monitor open nobody reads the log: each print would then
    // wait up to 2 s (20 x 100 ms in HWCDC) and freeze the UI. Drop log lines instead of waiting.
    Serial.setTxTimeoutMs(0);
    Wire.setBufferSize(256);      // SensorLib reads the IMU FIFO 240 bytes at a time; the default 128 jams it
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    settingsLoad();
    batteryBegin();
    batteryUpdate(true);
    Serial.printf("horodyne: v%s.%d built %s, reset reason %d\n", HORODYNE_VERSION, BUILD_NUMBER, BUILD_STAMP, (int)esp_reset_reason());
    displayBegin();
    uiSplash();                   // covers the IMU setup and the 1 s RTC check below
    lv_refr_now(NULL);
    displaySetBacklight(uiBrightnessPercent());
    uint32_t splashAt = millis();

    if (!imuBegin()) Serial.println("imu: setup failed, steps or raise may not work");
    clockBegin();
    stepsRestore();
    activityUpdate();
    while (millis() - splashAt < 1500) delay(10);

    uiBegin();
    lv_timer_handler();
    lv_refr_now(NULL);

    xTaskCreatePinnedToCore(netWorker, "net", 16384, NULL, 1, &netTask, 0);
    if (batteryRadioOk() && netHasCredentials()) netStart(NET_SYNC);   // battery first: the check starts Wi-Fi
    lastInputMs = millis();
}

void loop() {
    esp_task_wdt_reset();
    uint32_t now = millis();
    netFinish();

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
        case REQ_SYNC:          netStart(NET_SYNC); break;
        case REQ_GH_SYNC:       if (netStart(NET_GITHUB)) uiRefresh(); break;
        case REQ_PORTAL_START:
            // ponytail: holds the loop until the job ends (seconds); the portal needs Wi-Fi to itself.
            while (netBusy) { esp_task_wdt_reset(); delay(50); }
            netFinish();
            netPortalStart();
            break;
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
    uint32_t idle = millis() - lastInputMs;   // not now: lastInputMs can be newer than the start of this pass
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
