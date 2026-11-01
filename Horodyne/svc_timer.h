#pragma once
// Countdown timer and stopwatch. Both run on millis(), which keeps counting through light sleep.
#include <Arduino.h>

enum CdState : uint8_t { CD_IDLE, CD_RUNNING, CD_PAUSED, CD_DONE };

struct Countdown {
    CdState  state;
    uint16_t setMin;   // chosen length, 1-99 minutes
    uint32_t endMs;    // running: millis() when it reaches zero
    uint32_t leftMs;   // paused: time left
    uint32_t doneAt;   // millis() when it reached zero
};

struct Stopwatch {
    bool     running;
    uint32_t startMs;   // running: millis() at the last start
    uint32_t accMs;     // time from earlier runs
    uint8_t  laps;
    uint32_t lapBase;   // elapsed time at the last lap
    uint32_t lastLap;   // length of the last lap
};

extern Countdown countdown;
extern Stopwatch stopwatch;

uint32_t cdLeftMs();          // the set length while idle, 0 once done
void     cdStartPause();      // idle -> running -> paused -> running
void     cdCancel();          // back to idle; also silences the alarm
bool     cdCheckDone();       // call often: true once, when it reaches zero
bool     cdRinging();         // done and still within TIMER_RING_MS

uint32_t swElapsedMs();
void     swStartStop();
void     swLapReset();        // running: lap; stopped: reset
