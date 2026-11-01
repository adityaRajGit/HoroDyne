#include "svc_activity.h"
#include "svc_clock.h"
#include "hal_imu.h"
#include "config.h"
#include <string.h>

Activity activity = {};
ActivityStats activityStats = {};
static int      lastDay = -1;
static int      lastBin = -1;   // 2-hour block of the previous update

// Step detector on the wrist's |a| (g, IMU_ODR_HZ): a step is a swing of STEP_PEAK_G above the
// running average (after one as far below it), at least STEP_MIN_GAP_S after the last. Steps count
// only once STEP_ENTRY come in a row (each within STEP_MAX_GAP_S), then all of them do: a wave of
// the hand counts nothing.
// ponytail: plain peak counting; a steady vibration (bus, bike) can pass as steps. Add a cadence
// regularity check if that shows up in real use.
static float    avgG = 1.0f;
static bool     armed = true;
static uint32_t sinceStep = 1000;   // samples since the last peak
static uint8_t  streak = 0;

static uint32_t stepFeed(const float *g, int n) {
    uint32_t steps = 0;
    for (int i = 0; i < n; i++) {
        avgG += (g[i] - avgG) * 0.05f;   // ~0.6 s: gravity and slow changes of posture
        float d = g[i] - avgG;
        if (d > activityStats.maxSwing) activityStats.maxSwing = d;
        if (++sinceStep > STEP_MAX_GAP_S * IMU_ODR_HZ) streak = 0;   // the walk ended
        if (armed && d > STEP_PEAK_G && sinceStep >= STEP_MIN_GAP_S * IMU_ODR_HZ) {
            armed = false; sinceStep = 0;
            activityStats.peaks++;
            if (streak >= STEP_ENTRY) steps++;
            else if (++streak == STEP_ENTRY) steps += STEP_ENTRY;   // the run is real: count it from its start
        } else if (d < -STEP_PEAK_G) {
            armed = true;    // swung as far below the average: ready for the next peak
        }
    }
    return steps;
}

// Runs every second while awake and every STEP_POLL_MS asleep: the IMU FIFO holds ~4 s of samples.
void activityUpdate() {
    static float g[128];
    int n = imuReadSamples(g, 128);
    if (n < 0) return;                                       // no IMU
    activityStats.updates++;
    activityStats.samples += n;
    if (n == 0) activityStats.emptyReads++;
    uint32_t delta = stepFeed(g, n);

    int day = clockDayOfYear();
    if (day < 0) { activity.steps += delta; return; }   // no clock yet: total only, no history
    if (lastDay >= 0 && day != lastDay) {
        memset(&activity, 0, sizeof(activity));
        delta = 0;      // counted before midnight: the 2-hourly wake keeps this small
        lastBin = -1;
    }
    lastDay = day;

    activity.steps += delta;
    int hour = clockHour();
    if (hour < 0) return;
    // The sleeping watch wakes just after each 2-hour boundary, so steps read in a new block
    // were taken in the block of the previous update.
    int bin = lastBin >= 0 ? lastBin : hour / 2;
    lastBin = hour / 2;
    if (delta > 0) {
        uint32_t b = activity.bins[bin] + delta;
        activity.bins[bin] = b > 65535 ? 65535 : b;
        Serial.printf("steps: +%lu today %lu (block %02d-%02d)\n", (unsigned long)delta,
                      (unsigned long)activity.steps, bin * 2, bin * 2 + 2);
    }
}

float activityKm() { return activity.steps * 0.75f / 1000.0f; }
