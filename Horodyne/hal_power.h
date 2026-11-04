#pragma once
// PWR key, battery power latch, and light sleep.
#include <Arduino.h>

enum PwrEvent : uint8_t { PWR_NONE, PWR_CLICK, PWR_DOUBLE, PWR_LONG };

enum WakeReason : uint8_t { WAKE_TOUCH, WAKE_PWR, WAKE_IMU, WAKE_TIMER, WAKE_OTHER };

void     powerLatch();        // first thing in setup(): keep the board on when running from battery
void     powerOff();          // releases the latch; on USB power, waits for a PWR hold instead
PwrEvent powerPoll();         // call often; returns click, double-click or 2 s hold
bool     powerKeyDown();
void     buzzerSet(bool on);  // passive buzzer tone on/off; cheap to call every loop
// Light sleep until touch, PWR, IMU interrupt, or the timer.
// imuWake=false skips the IMU as a wake source (used for the raise cooldown).
// stayAwake=true waits for the same events without sleeping (Wi-Fi in use).
WakeReason powerLightSleep(uint64_t timerUs, bool imuWake, bool stayAwake = false);
