#include "svc_timer.h"
#include "config.h"

Countdown countdown = { CD_IDLE, 5, 0, 0, 0 };
Stopwatch stopwatch = {};

uint32_t cdLeftMs() {
    switch (countdown.state) {
        case CD_RUNNING: { int32_t left = (int32_t)(countdown.endMs - millis()); return left > 0 ? left : 0; }
        case CD_PAUSED:  return countdown.leftMs;
        case CD_DONE:    return 0;
        default:         return countdown.setMin * 60000UL;
    }
}

void cdStartPause() {
    switch (countdown.state) {
        case CD_IDLE:    countdown.endMs = millis() + countdown.setMin * 60000UL; countdown.state = CD_RUNNING; break;
        case CD_PAUSED:  countdown.endMs = millis() + countdown.leftMs; countdown.state = CD_RUNNING; break;
        case CD_RUNNING: countdown.leftMs = cdLeftMs(); countdown.state = CD_PAUSED; break;
        default: break;
    }
}

void cdCancel() { countdown.state = CD_IDLE; }

bool cdCheckDone() {
    if (countdown.state != CD_RUNNING || cdLeftMs() > 0) return false;
    countdown.state = CD_DONE;
    countdown.doneAt = millis();
    return true;
}

bool cdRinging() { return countdown.state == CD_DONE && millis() - countdown.doneAt < TIMER_RING_MS; }

uint32_t swElapsedMs() {
    return stopwatch.accMs + (stopwatch.running ? millis() - stopwatch.startMs : 0);
}

void swStartStop() {
    if (stopwatch.running) stopwatch.accMs = swElapsedMs();
    else                   stopwatch.startMs = millis();
    stopwatch.running = !stopwatch.running;
}

void swLapReset() {
    if (!stopwatch.running) { stopwatch = {}; return; }
    uint32_t now = swElapsedMs();
    stopwatch.lastLap = now - stopwatch.lapBase;
    stopwatch.lapBase = now;
    stopwatch.laps++;
}
