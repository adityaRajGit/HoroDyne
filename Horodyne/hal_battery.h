#pragma once
// Battery voltage, percentage and charge detection.
#include <Arduino.h>

struct BatteryState {
    bool    present;    // false when running from USB with no cell attached
    float   volts;      // smoothed resting voltage
    uint8_t percent;    // what the UI shows
    bool    charging;
    float   daysLeft;   // < 0 while there is not enough history to estimate
};

extern BatteryState battery;

void batteryBegin();
// atRest: true right after waking, before the backlight turns on.
void batteryUpdate(bool atRest);
// Wi-Fi on/off: readings are skipped while it is on and for BAT_RADIO_SETTLE_MS after.
void batteryRadio(bool on);

// On-battery log: a reading every 10 min while unplugged (kept in RAM, ~24 h).
// Send 'b' over USB serial to print it: it shows the real discharge, which a USB log can't.
void batterySerialPoll();

// Exposed for testing and the Phase 0 serial log.
float   batteryReadVolts();
uint8_t batteryVoltsToPercent(float v);
