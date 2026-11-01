// PC test: a voltage jump while already charging must not rewrite the charger lift.
// Build 29 log: 25 % at 4.05 V on USB became 61 % at 4.014 V. Run: test/run_tests.sh
#include <cstdio>
#include <cassert>
#include "../Horodyne/hal_battery.cpp"

uint32_t fakeMillis = 0;
uint16_t fakePinMv = 0;
bool     fakeUsbHost = false;

static void step(float volts) {
    fakeMillis += 10000;
    fakePinMv = (uint16_t)lroundf(volts * 1000.0f / BAT_DIVIDER);
    fakeUsbHost = true;
    batteryUpdate(true);
    printf("%.2f V -> %u%%\n", volts, battery.percent);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    step(4.05f);
    uint8_t before = battery.percent;
    assert(before <= 30);          // 4.05 V minus the 0.30 V lift

    step(4.15f); step(4.15f);      // current change on the charger, held for two readings
    step(4.05f); step(4.05f); step(4.05f);
    assert(battery.percent <= before + 6);   // the average moved 20 mV: one step of the flat curve
    assert(chgOffset == BAT_CHG_OFFSET_V);
    puts("charge offset test passed");
}
