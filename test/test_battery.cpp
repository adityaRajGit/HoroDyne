// PC test for Horodyne/hal_battery.cpp: charging detection and the percent shown.
// Run: test/run_tests.sh
#include <cstdio>
#include <cassert>
#include "../Horodyne/hal_battery.cpp"

uint32_t fakeMillis = 0;
uint16_t fakePinMv = 0;
bool     fakeUsbHost = false;

// One battery reading 10 s after the last, with the cell at `volts`.
static void step(float volts, bool usbHost = false, bool atRest = true) {
    fakeMillis += 10000;
    fakePinMv = (uint16_t)lroundf(volts * 1000.0f / BAT_DIVIDER);
    fakeUsbHost = usbHost;
    batteryUpdate(atRest);
    printf("%6.2f V usb=%d -> %3u%% %s\n", volts, usbHost, battery.percent, battery.charging ? "CHARGING" : "");
}

int main() {
    assert(batteryVoltsToPercent(3.20f) == 0);
    assert(batteryVoltsToPercent(4.25f) == 100);
    assert(batteryVoltsToPercent(3.80f) == 40);

    // The bug report: boots on a PC's USB reading 4.17 V. That is the charger's voltage, not the cell's.
    step(4.17f, true);
    assert(battery.charging);
    assert(battery.percent <= 85);   // was 97 % before

    // Cable out: the voltage falls to the real, nearly empty cell.
    step(3.60f, false);
    assert(!battery.charging);
    assert(battery.percent <= 5);

    // Resting on battery, readings wobble up: the percent must not climb.
    step(3.62f); step(3.63f);
    assert(!battery.charging && battery.percent <= 5);

    // Wall charger plugged in (no USB frames): the voltage jumps at once and stays up.
    step(3.80f);
    assert(!battery.charging);   // one reading could be a spike
    step(3.80f);
    assert(battery.charging);
    assert(battery.percent <= 10);   // the jump is the charger, not charge

    // Charging: the percent climbs as the cell does, never above 99.
    for (float v = 3.82f; v <= 4.20f; v += 0.02f) step(v);
    assert(battery.charging && battery.percent <= 99);
    uint8_t charged = battery.percent;

    // Unplug: the percent is read again from the resting voltage.
    step(4.08f); step(4.08f);
    assert(!battery.charging);
    assert(battery.percent == batteryVoltsToPercent(4.08f));
    assert(battery.percent > charged - 30);

    // Computer's USB plugged in with only a small voltage change: USB frames alone mean charging.
    step(4.10f, true);
    assert(battery.charging);
    step(4.05f, false);
    assert(!battery.charging);

    // Worn cell on battery: screen on sags it 150 mV, screen off it recovers. Not a charger.
    step(3.90f, false, false); step(3.75f, false, true); step(3.90f, false, false);
    assert(!battery.charging);

    // Resting higher for over 10 minutes: the percent may climb again.
    for (int i = 0; i < 70; i++) step(3.85f);
    uint8_t high = battery.percent;
    assert(!battery.charging && high == batteryVoltsToPercent(battery.volts));

    // Wi-Fi on: sagged readings are ignored, and for a moment after it turns off.
    batteryRadio(true);
    step(3.50f); step(3.50f);
    batteryRadio(false);
    batteryUpdate(true);   // same millis(): still settling
    assert(battery.percent == high && !battery.charging);

    // A short dip shows, then recovers after 10 minutes at rest, not after 5.
    for (int i = 0; i < 6; i++) step(3.83f);
    assert(battery.percent < high);
    for (int i = 0; i < 30; i++) step(3.85f);
    assert(battery.percent < batteryVoltsToPercent(battery.volts));
    for (int i = 0; i < 40; i++) step(3.85f);
    assert(battery.percent == batteryVoltsToPercent(battery.volts));

    // Screen on for 2 minutes on a worn cell, sagging 120 mV: that is load, not charge used.
    uint8_t before = battery.percent;
    for (int i = 0; i < 12; i++) step(3.73f, false, false);
    assert(battery.percent == before && !battery.charging);
    step(3.85f);   // screen off again
    assert(battery.percent == before && !battery.charging);

    // Long screen-on session, then resting: the recovery from sag is not a slow charger rise.
    for (int i = 0; i < 18; i++) step(3.70f, false, false);
    for (int i = 0; i < 18; i++) { step(3.85f); assert(!battery.charging); }
    assert(battery.percent == before);

    // One spike is not a charger; a jump that holds is.
    step(3.95f); step(3.85f);
    assert(!battery.charging);
    step(3.95f); step(3.96f);
    assert(battery.charging);

    // On-battery log: nothing while charging, the first reading after unplugging at once,
    // then one every 10 minutes. 'b' over serial prints it.
    step(3.96f, true);
    uint8_t n0 = logCount;
    step(3.96f, true);
    assert(battery.charging && logCount == n0);
    step(3.90f, false);
    assert(!battery.charging && logCount == n0 + 1);
    for (int i = 0; i < 59; i++) step(3.90f);
    assert(logCount == n0 + 1);
    step(3.90f);
    assert(logCount == n0 + 2);
    Serial.pending = 'b';
    batterySerialPoll();
    assert(!Serial.available());

    // The walk log (2026-10-26): on USB the cell read 4.19 V, unplugged it rested at 3.84 V,
    // yet charging had shown 87 %. Unplugging teaches the real 0.35 V lift...
    step(4.19f, true); step(4.19f, true); step(4.19f, true);
    chgOffset = 0.10f;                  // the old default, as when no plug-in jump was ever measured
    step(3.84f, false);
    assert(!battery.charging && chgOffset > 0.25f && chgOffset < 0.45f);
    uint8_t rested = battery.percent;
    // ...so the next plug-in, made while asleep (no same-kind jump to measure), doesn't jump to 87 %.
    step(3.70f, false, false);          // screen on, on battery
    step(4.19f, true);                  // woke on the charger
    step(4.19f, true);
    assert(battery.charging && battery.percent <= rested + 5);
    // The charger holds ~4.2 V near full, so the percent creeps up instead of stalling: ~1 % per
    // 90 s, never past 99 while charging.
    uint8_t before2 = battery.percent;
    for (int i = 0; i < 60; i++) step(4.19f, true);   // 10 minutes
    assert(battery.percent >= before2 + 5 && battery.percent <= before2 + 8);
    for (int i = 0; i < 600; i++) step(4.19f, true);  // 100 minutes
    assert(battery.percent == 99);

    puts("battery: all checks passed");
}
