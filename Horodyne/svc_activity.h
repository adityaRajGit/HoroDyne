#pragma once
// Daily steps counted in software from the IMU FIFO, zeroed at midnight, with 2-hour history.
#include <Arduino.h>

struct Activity {
    uint32_t steps;
    uint16_t bins[12];   // steps per 2-hour block, 00:00-02:00 first
};

extern Activity activity;

void  activityUpdate();          // on every wake and every few seconds while awake
// Counters since boot, read over USB with the "s" command: they survive an unplugged walk, so they
// show whether samples arrived at all and how big the swings were.
struct ActivityStats { uint32_t updates, samples, emptyReads, peaks; float maxSwing; };
extern ActivityStats activityStats;
float activityKm();              // 0.75 m per step
