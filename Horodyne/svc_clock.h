#pragma once
// Time: NTP over Wi-Fi, time zones, and the PCF85063 RTC when it works.
#include <Arduino.h>
#include <time.h>

struct TzEntry { const char *label; const char *posix; };
extern const TzEntry TZ_LIST[];
extern const uint8_t TZ_COUNT;

void clockBegin();               // sets the time zone, probes the RTC, loads time from it if valid
void clockApplyTz();             // after settings.tzIndex changes
bool clockValid();               // false until the time has been set once
bool clockRtcOk();               // the PCF85063 answered and is ticking
bool clockNtpSync();             // needs Wi-Fi already connected; also writes the RTC
void clockCorrectFromRtc();      // re-reads the RTC every 10 min to cancel sleep drift
void clockNextTz();
bool clockTakeJump();            // true once after the clock was corrected or the zone changed

// Formatting for the UI
void clockTimeText(char *out, size_t n);   // "10:42" or "9:42"
void clockDateText(char *out, size_t n);   // "SAT 10 OCT"
int  clockDayNumber();                     // year * 1000 + day of year, -1 while the time is unknown
int  clockHour();                          // 0-23, -1 while unknown
uint32_t clockMsUntilMidnight();           // 0 while unknown
