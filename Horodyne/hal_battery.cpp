#include "hal_battery.h"
#include "config.h"
#include <driver/usb_serial_jtag.h>

BatteryState battery = { false, 0, 0, false, -1 };

// Resting voltage of a single-cell LiPo, every 5 % from 0 to 100.
// A straight 3.3-4.2 V scale is badly wrong in the flat middle of the curve.
static const float CURVE[21] = {
    3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f, 3.82f, 3.84f,
    3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f
};

static float    emaVolts = 0;
static float    restVolts = 0;    // average of resting readings only (0 = none yet); sets the percent
static float    trendRefVolts = 0;
static uint32_t trendRefMs = 0;
static bool     haveReading = false;
static bool     lastUsbHost = false;
static float    chgOffset = BAT_CHG_OFFSET_V;
static float    lastVolts = 0;
static bool     lastAtRest = true;
static int8_t   jumpDir = 0;      // +1/-1: a jump seen once, waiting for the next reading to confirm
static float    jumpBase = 0;     // the reading before that jump
static bool     jumpOnCharger = false;  // already charging when it began: not a plug-in
static uint32_t climbSinceMs = 0; // when a resting reading first sat above the shown percent
static float    unplugVolts = 0;  // charging average when the cable came out (0 = lift already learned)
static uint32_t unplugMs = 0;
static uint32_t creepMs = 0;      // last +1 % while the charger held the voltage near full
static bool     radioOn = false;
static uint32_t radioOffMs = 0;

void batteryRadio(bool on) { radioOn = on; if (!on) radioOffMs = millis(); }

// History for the days-left estimate: one sample every 30 minutes while discharging.
static const uint8_t HIST = 48;
static uint8_t  histPct[HIST];
static uint32_t histMs[HIST];
static uint8_t  histCount = 0, histHead = 0;
static uint32_t lastHistMs = 0;

// On-battery log, one record per 10 minutes while discharging.
struct BatLog { uint16_t min; uint16_t rawMv, restMv; uint8_t pct, atRest; };
static const uint8_t LOG_N = 144;
static BatLog   logBuf[LOG_N];
static uint8_t  logHead = 0, logCount = 0;
static bool     logDue = true;   // log the first reading after the cable comes out at once
static uint32_t lastLogMs = 0;

static void logReading(float raw, bool atRest, uint32_t now) {
    if (battery.charging) { logDue = true; return; }
    if (!logDue && now - lastLogMs < 600000UL) return;
    logBuf[logHead] = { (uint16_t)(now / 60000UL), (uint16_t)(raw * 1000), (uint16_t)(restVolts * 1000),
                        battery.percent, atRest };
    logHead = (logHead + 1) % LOG_N;
    if (logCount < LOG_N) logCount++;
    logDue = false; lastLogMs = now;
}

void serialCommand(const char *line) __attribute__((weak));   // Horodyne.ino

// One command per line from the serial port: "b" dumps the battery log, the rest go to serialCommand.
void batterySerialPoll() {
    static char line[192];
    static uint8_t len = 0;
    bool done = false;
    while (Serial.available() && !done) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') done = len > 0;
        else if (len + 1 < sizeof(line)) line[len++] = c;
    }
    if (!done) return;
    line[len] = 0;
    len = 0;
    if (line[0] != 'b' || line[1]) { if (serialCommand) serialCommand(line); return; }
    Serial.printf("batlog: %u records (min since boot, raw V, resting avg V, %%, at rest)\n", logCount);
    for (uint8_t i = 0; i < logCount; i++) {
        const BatLog &r = logBuf[(logHead + LOG_N - logCount + i) % LOG_N];
        Serial.printf("batlog: %5u %.3f %.3f %3u%% %s\n", r.min, r.rawMv / 1000.0f, r.restMv / 1000.0f,
                      r.pct, r.atRest ? "rest" : "screen on");
    }
}

void batteryBegin() {
    pinMode(PIN_BAT_ADC, INPUT);
    analogReadResolution(12);
    // The pin sees VBAT/3, at most ~1.45 V; 6 dB (0-1.75 V on the S3) is more accurate than 11 dB.
    analogSetPinAttenuation(PIN_BAT_ADC, ADC_6db);
}

float batteryReadVolts() {
    // analogReadMilliVolts applies the chip's factory ADC calibration.
    uint16_t s[32];
    for (int i = 0; i < 32; i++) s[i] = analogReadMilliVolts(PIN_BAT_ADC);
    // Insertion sort, then average the middle 16 to drop outliers.
    for (int i = 1; i < 32; i++) {
        uint16_t v = s[i]; int j = i - 1;
        while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
        s[j + 1] = v;
    }
    uint32_t sum = 0;
    for (int i = 8; i < 24; i++) sum += s[i];
    return (sum / 16.0f) / 1000.0f * BAT_DIVIDER;
}

uint8_t batteryVoltsToPercent(float v) {
    if (v <= CURVE[0])  return 0;
    if (v >= CURVE[20]) return 100;
    for (int i = 1; i <= 20; i++) {
        if (v < CURVE[i]) {
            float f = (v - CURVE[i - 1]) / (CURVE[i] - CURVE[i - 1]);
            return (uint8_t)roundf((i - 1 + f) * 5.0f);
        }
    }
    return 100;
}

static void updateDaysLeft(uint32_t now) {
    if (battery.charging) { histCount = 0; histHead = 0; battery.daysLeft = -1; return; }
    if (histCount == 0 || now - lastHistMs >= 30UL * 60UL * 1000UL) {
        histPct[histHead] = battery.percent;
        histMs[histHead]  = now;
        histHead = (histHead + 1) % HIST;
        if (histCount < HIST) histCount++;
        lastHistMs = now;
    }
    if (histCount < 5) { battery.daysLeft = -1; return; }   // need about 2 hours of history
    uint8_t oldest = (histHead + HIST - histCount) % HIST;
    float usedPct = (float)histPct[oldest] - battery.percent;
    float hours   = (now - histMs[oldest]) / 3600000.0f;
    if (usedPct <= 0.5f || hours <= 0) { battery.daysLeft = -1; return; }
    battery.daysLeft = battery.percent / (usedPct / hours) / 24.0f;
}

// Restart the trend window and the average from a fresh reading.
static void rebase(float v, uint32_t now) { emaVolts = v; trendRefVolts = 0; trendRefMs = now; restVolts = 0; }

void batteryUpdate(bool atRest) {
    // Wi-Fi draws a few hundred mA and sags the cell; keep the last value until it has settled.
    if (radioOn || (radioOffMs && millis() - radioOffMs < BAT_RADIO_SETTLE_MS)) return;
    float v = batteryReadVolts();
    battery.present = v > 2.5f;   // a LiPo never rests this low; this is USB with no cell
    if (!battery.present) { battery.volts = v; battery.charging = false; battery.daysLeft = -1; return; }
    if (!atRest) v += BAT_LOAD_COMP_V;
    uint32_t now = millis();
    bool wasCharging = battery.charging;

    // The board has no charge-status pin. Three signals, best first:
    // 1. USB frames from a computer (a wall charger sends none).
    bool usbHost = usb_serial_jtag_is_connected();
    // 2. Plugging in or pulling the cable moves the cell voltage at once (charge current x
    //    internal resistance). 3. Slow rise over two minutes (wall charger, no jump seen).
    if (!haveReading) {
        rebase(v, now); haveReading = true;
        battery.charging = usbHost;
        battery.percent = batteryVoltsToPercent(usbHost ? v - chgOffset : v);
    } else if (atRest != lastAtRest) {
        // Screen on vs off moves the reading by load x cell resistance, which can pass for a
        // cable going in or out on a worn cell; only compare readings taken the same way.
        emaVolts += 0.2f * (v - emaVolts);
        jumpDir = 0;
    } else {
        // A jump vs the last raw reading (the average lags) must hold for a second reading:
        // one spike or sag is not a cable going in or out.
        float ref = jumpDir ? jumpBase : lastVolts;
        int8_t dir = v - ref > BAT_CHG_STEP_V ? 1 : ref - v > BAT_CHG_STEP_V ? -1 : 0;
        if (dir == 0) { jumpDir = 0; emaVolts += 0.2f * (v - emaVolts); }
        else if (dir != jumpDir) { jumpDir = dir; jumpBase = ref; jumpOnCharger = wasCharging; }
        else {
            jumpDir = 0;
            battery.charging = dir > 0;
            // Only a jump from off the charger measures the lift: on it, a current change would
            // shrink the offset or reset the average, and the percent never falls on the charger.
            if (dir > 0 && !jumpOnCharger) { chgOffset = v - ref; rebase(v, now); }
            else emaVolts += 0.2f * (v - emaVolts);
        }
    }
    lastVolts = v; lastAtRest = atRest;
    if (atRest) restVolts = restVolts ? restVolts + 0.2f * (v - restVolts) : v;
    if (usbHost) battery.charging = true;
    else if (lastUsbHost) battery.charging = false;   // computer unplugged
    lastUsbHost = usbHost;

    // Trend on resting readings only: recovering from screen-on sag would look like a charger.
    if (restVolts && (!trendRefVolts || now - trendRefMs >= 120000UL)) {
        float delta = trendRefVolts ? restVolts - trendRefVolts : 0;
        if (delta > 0.030f) battery.charging = true;
        else if (delta < -0.010f && !usbHost) battery.charging = false;
        trendRefVolts = restVolts; trendRefMs = now;
    }

    if (wasCharging && !battery.charging) {
        // Off the charger: the estimate made while charging is only a guess, so start over.
        unplugVolts = emaVolts; unplugMs = now;
        rebase(v, now);
        if (atRest) restVolts = v;
        climbSinceMs = 0;
    }
    // First resting reading after unplugging: charging average minus this is the lift the charge
    // current really gives on this cell and charger (a plug-in made while asleep never shows a
    // same-kind jump to measure it). A worn cell lifts ~0.35 V, not the 0.1 V of a healthy one.
    if (unplugVolts && atRest && !battery.charging && restVolts) {
        if (now - unplugMs < 20UL * 60000UL) chgOffset = fminf(fmaxf(unplugVolts - v, 0.05f), 0.60f);
        unplugVolts = 0;
        battery.percent = batteryVoltsToPercent(v);
    }
    if (battery.charging) {
        if (!wasCharging) creepMs = now;
        climbSinceMs = 0;
        // The charger lifts the reading, so take that back off before looking up the percent.
        uint8_t pct = batteryVoltsToPercent(emaVolts - chgOffset);
        if (pct > battery.percent) battery.percent = pct;
        // ponytail: near full the charger holds ~4.2 V, so the voltage stops showing progress;
        // creep +1 % per BAT_CV_CREEP_MS instead of stalling. A fuel-gauge chip is the real fix.
        else if (v >= BAT_CV_VOLTS && now - creepMs >= BAT_CV_CREEP_MS) { battery.percent++; creepMs = now; }
        if (pct > battery.percent || v < BAT_CV_VOLTS) creepMs = now;
        if (battery.percent > 99) battery.percent = 99;   // 100 only once off the charger and full
    } else if (restVolts) {
        // Only resting readings move the percent: on a worn cell the screen-on sag is 100 mV+,
        // which the flat middle of the curve turns into a 25 % drop that would then stick.
        // Drop at once; climb only after resting readings stay higher for BAT_CLIMB_MS.
        uint8_t pct = batteryVoltsToPercent(restVolts);
        if (pct < battery.percent || pct == 100) { battery.percent = pct; climbSinceMs = 0; }
        else if (pct == battery.percent) climbSinceMs = 0;
        else if (atRest) {
            if (!climbSinceMs) climbSinceMs = now;
            else if (now - climbSinceMs >= BAT_CLIMB_MS) { battery.percent = pct; climbSinceMs = 0; }
        }
    }
    battery.volts = restVolts ? restVolts : emaVolts;   // the Battery app shows what the % comes from
    updateDaysLeft(now);
    logReading(v, atRest, now);
    Serial.printf("bat: raw %.3f V avg %.3f V %u%% %s%s\n", v, emaVolts, battery.percent,
                  battery.charging ? "charging" : "on battery", usbHost ? " (usb host)" : "");
}
