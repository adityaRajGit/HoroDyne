#include "svc_clock.h"
#include "svc_settings.h"
#include "config.h"
#include <Wire.h>
#include <sys/time.h>
#include <esp_sntp.h>

const TzEntry TZ_LIST[] = {
    { "IST +5:30",   "IST-5:30" },
    { "UTC",         "UTC0" },
    { "LONDON",      "GMT0BST,M3.5.0/1,M10.5.0" },
    { "BERLIN",      "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "DUBAI",       "<+04>-4" },
    { "SINGAPORE",   "<+08>-8" },
    { "TOKYO",       "JST-9" },
    { "SYDNEY",      "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "NEW YORK",    "EST5EDT,M3.2.0,M11.1.0" },
    { "LOS ANGELES", "PST8PDT,M3.2.0,M11.1.0" },
};
const uint8_t TZ_COUNT = sizeof(TZ_LIST) / sizeof(TZ_LIST[0]);

static bool     rtcOk = false;
static uint32_t lastRtcReadMs = 0;
static volatile bool ntpDone = false;

// ----- PCF85063 over raw I2C ----------------------------------------------
static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool rtcRead(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(ADDR_RTC);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom((int)ADDR_RTC, (int)len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static bool rtcWrite(uint8_t reg, const uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(ADDR_RTC);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
    return Wire.endTransmission(true) == 0;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// The RTC stores UTC. Returns 0 if it can't be read or its time can't be trusted.
static time_t rtcGetUtc() {
    uint8_t t[7];
    if (!rtcRead(0x04, t, 7)) return 0;
    if (t[0] & 0x80) return 0;   // OS flag: the clock stopped at some point, time is invalid
    int sec = bcd2dec(t[0] & 0x7F), min = bcd2dec(t[1] & 0x7F), hour = bcd2dec(t[2] & 0x3F);
    int day = bcd2dec(t[3] & 0x3F), mon = bcd2dec(t[5] & 0x1F), year = 2000 + bcd2dec(t[6]);
    if (mon < 1 || mon > 12 || day < 1 || day > 31) return 0;
    return (time_t)(daysFromCivil(year, mon, day) * 86400LL + hour * 3600 + min * 60 + sec);
}

static void rtcSetUtc(time_t utc) {
    struct tm tmv;
    gmtime_r(&utc, &tmv);
    uint8_t t[7] = {
        dec2bcd(tmv.tm_sec),         // writing seconds also clears the OS flag
        dec2bcd(tmv.tm_min),
        dec2bcd(tmv.tm_hour),
        dec2bcd(tmv.tm_mday),
        (uint8_t)tmv.tm_wday,
        dec2bcd(tmv.tm_mon + 1),
        dec2bcd(tmv.tm_year % 100),
    };
    rtcWrite(0x04, t, 7);
}

// The board test failed the RTC because its seconds never advanced. One cause
// is simply the STOP bit (Control_1 bit 5) being set, which software can clear.
static void rtcProbe() {
    uint8_t ctrl;
    if (!rtcRead(0x00, &ctrl, 1)) { Serial.println("rtc: no answer at 0x51"); return; }
    if (ctrl & 0x20) {
        uint8_t cleared = ctrl & ~0x20;
        rtcWrite(0x00, &cleared, 1);
        Serial.println("rtc: STOP bit was set, cleared it");
    }
    uint8_t s1, s2;
    if (!rtcRead(0x04, &s1, 1)) return;
    delay(1100);
    if (!rtcRead(0x04, &s2, 1)) return;
    rtcOk = (s1 & 0x7F) != (s2 & 0x7F);
    Serial.printf("rtc: %s\n", rtcOk ? "ticking" : "not ticking, using Wi-Fi time only");
}

// ----- public -------------------------------------------------------------
void clockApplyTz() {
    uint8_t i = settings.tzIndex < TZ_COUNT ? settings.tzIndex : 0;
    setenv("TZ", TZ_LIST[i].posix, 1);
    tzset();
}

void clockNextTz() {
    settings.tzIndex = (settings.tzIndex + 1) % TZ_COUNT;
    settingsSave();
    clockApplyTz();
}

void clockBegin() {
    clockApplyTz();
    rtcProbe();
    if (rtcOk) {
        time_t utc = rtcGetUtc();
        if (utc > 1700000000) {
            struct timeval tv = { utc, 0 };
            settimeofday(&tv, nullptr);
            lastRtcReadMs = millis();
        }
    }
}

bool clockValid()  { return time(nullptr) > 1700000000; }
bool clockRtcOk()  { return rtcOk; }

void clockCorrectFromRtc() {
    if (!rtcOk || millis() - lastRtcReadMs < 10UL * 60UL * 1000UL) return;
    time_t utc = rtcGetUtc();
    lastRtcReadMs = millis();
    if (utc > 1700000000) {
        struct timeval tv = { utc, 0 };
        settimeofday(&tv, nullptr);
    }
}

static void onNtp(struct timeval *) { ntpDone = true; }

bool clockNtpSync() {
    time_t before = time(nullptr);
    uint32_t beforeMs = millis();
    ntpDone = false;
    sntp_set_time_sync_notification_cb(onNtp);
    uint8_t i = settings.tzIndex < TZ_COUNT ? settings.tzIndex : 0;
    configTzTime(TZ_LIST[i].posix, "time.google.com", "time.apple.com", "time.windows.com");   // pool.ntp.org timed out on the home network
    uint32_t t0 = millis();
    while (!ntpDone && millis() - t0 < 10000) delay(50);
    if (!ntpDone) return false;

    time_t now = time(nullptr);
    if (before > 1700000000) {
        // How far the sleeping clock drifted since the last correction (Phase 0 measurement).
        long expected = (long)(before + (millis() - beforeMs) / 1000);
        Serial.printf("clock: drift %+ld s\n", (long)(now - expected));
    }
    if (rtcOk) { rtcSetUtc(now); lastRtcReadMs = millis(); }
    return true;
}

void clockTimeText(char *out, size_t n) {
    if (!clockValid()) { strlcpy(out, "--:--", n); return; }
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (settings.h24) snprintf(out, n, "%02d:%02d", t.tm_hour, t.tm_min);
    else              snprintf(out, n, "%d:%02d", t.tm_hour % 12 ? t.tm_hour % 12 : 12, t.tm_min);
}

void clockDateText(char *out, size_t n) {
    if (!clockValid()) { strlcpy(out, "SET UP WI-FI", n); return; }
    static const char *DAYS[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    static const char *MONS[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(out, n, "%s %d %s", DAYS[t.tm_wday], t.tm_mday, MONS[t.tm_mon]);
}

int clockDayOfYear() {
    if (!clockValid()) return -1;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_yday;
}

int clockHour() {
    if (!clockValid()) return -1;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_hour;
}

uint32_t clockMsUntilMidnight() {
    if (!clockValid()) return 0;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    uint32_t secs = (23 - t.tm_hour) * 3600 + (59 - t.tm_min) * 60 + (60 - t.tm_sec);
    return secs * 1000UL;
}
