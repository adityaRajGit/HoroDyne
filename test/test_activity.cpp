// PC test for Horodyne/svc_activity.cpp (step totals) and svc_timer.cpp (timer, stopwatch).
// Run: test/run_tests.sh
#include <cstdio>
#include <cassert>
#include "../Horodyne/svc_activity.cpp"
#include "../Horodyne/svc_timer.cpp"

uint32_t fakeMillis = 0;
uint16_t fakePinMv = 0;
static bool i2cOk = true;
static int  day = 100, hour = 9;

// The IMU FIFO, faked: samples queued by the test, read like the chip (up to 128 per call).
static float fifo[8192];
static int   fifoHead = 0, fifoTail = 0;
int imuReadSamples(float *g, int max) {
    if (!i2cOk) return -1;
    int n = fifoTail - fifoHead;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) g[i] = fifo[fifoHead + i];
    fifoHead += n;
    return n;
}
int  clockDayOfYear() { return day; }
int  clockHour() { return hour; }

// Wrist |a| while walking: one swing of ~0.3 g per step at 1.8 steps/s. Still: 1 g with a little noise.
static void walk(int steps) {
    int per = (int)(IMU_ODR_HZ / 1.8f);
    for (int s = 0; s < steps; s++)
        for (int i = 0; i < per; i++) fifo[fifoTail++] = 1.0f + 0.3f * sinf(6.2832f * i / per);
}
static void still(int seconds) {
    for (int i = 0; i < seconds * IMU_ODR_HZ; i++) fifo[fifoTail++] = 1.0f + (i & 1 ? 0.02f : -0.02f);
}
static void drain() { while (fifoHead < fifoTail) activityUpdate(); }

int main() {
    // A walk counts every step, the first STEP_ENTRY included.
    walk(20); still(3); drain();
    assert(activity.steps == 20 && activity.bins[4] == 20);

    // Sitting still counts nothing; nor does a short wave of the hand (fewer than STEP_ENTRY in a row).
    still(10); drain();
    walk(STEP_ENTRY - 2); still(3); drain();
    assert(activity.steps == 20);

    // I2C error: the samples stay in the chip and are counted on the next good read.
    walk(10); still(3);
    i2cOk = false; activityUpdate(); i2cOk = true;
    assert(activity.steps == 20);
    drain();
    assert(activity.steps == 30);

    // The 2-hourly wake lands just after 10:00: the steps it reads were taken before 10:00...
    walk(7); hour = 10; activityUpdate();
    assert(activity.steps == 37 && activity.bins[4] == 37 && activity.bins[5] == 0);
    // ...later ones go to the new block.
    still(3); walk(8); still(3); drain();
    assert(activity.steps == 45 && activity.bins[5] == 8);

    // Midnight: the new day starts at zero.
    still(1); day = 101; hour = 0; drain();
    assert(activity.steps == 0 && activity.bins[5] == 0);
    walk(10); still(3); drain();
    assert(activity.steps == 10 && activity.bins[0] == 10);

    // A real 20-step walk recorded on the wrist (smooth, ~0.1 g swings) counts 18-23.
    {
        static const short rec[] = {
#include "walk_20steps.inc"
        };
        uint32_t before = activity.steps;
        for (short v : rec) fifo[fifoTail++] = v / 100.0f;
        drain();
        uint32_t got = activity.steps - before;
        assert(got >= 18 && got <= 23);
    }

    // Countdown: 1 min, pause 10 s in the middle, ends at the right time, across millis() wrap.
    fakeMillis = 0xFFFF0000u;
    countdown.setMin = 1;
    assert(cdLeftMs() == 60000);
    cdStartPause();
    fakeMillis += 20000; assert(cdLeftMs() == 40000);
    cdStartPause();                       // pause
    fakeMillis += 10000; assert(cdLeftMs() == 40000 && !cdCheckDone());
    cdStartPause();                       // resume
    fakeMillis += 39999; assert(!cdCheckDone() && cdLeftMs() == 1);
    fakeMillis += 1;     assert(cdCheckDone() && !cdCheckDone() && cdRinging());
    fakeMillis += TIMER_RING_MS; assert(!cdRinging());
    cdCancel(); assert(countdown.state == CD_IDLE && cdLeftMs() == 60000);

    // Stopwatch: run 5 s, stop 3 s, run 2 s with a lap at 6 s.
    swStartStop(); fakeMillis += 5000;
    swStartStop(); fakeMillis += 3000; assert(swElapsedMs() == 5000);
    swStartStop(); fakeMillis += 1000; swLapReset(); fakeMillis += 1000;
    assert(swElapsedMs() == 7000 && stopwatch.laps == 1 && stopwatch.lastLap == 6000);
    swStartStop(); swLapReset(); assert(swElapsedMs() == 0 && stopwatch.laps == 0);

    printf("activity + timer tests passed\n");
}
