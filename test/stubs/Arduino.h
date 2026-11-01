#pragma once
// Just enough of Arduino.h for hal_battery.cpp to build on a PC.
#include <cstdint>
#include <cmath>
#define INPUT 1
#define ADC_6db  2
#define ADC_11db 3
inline void pinMode(int, int) {}
inline void analogReadResolution(int) {}
inline void analogSetPinAttenuation(int, int) {}
extern uint32_t fakeMillis;
extern uint16_t fakePinMv;   // what the ADC pin sees, after the divider
inline uint32_t millis() { return fakeMillis; }
inline uint16_t analogReadMilliVolts(int) { return fakePinMv; }
#include <cstdio>
struct FakeSerial {
    int pending = -1;   // one byte "typed" by a test, -1 = none
    template <class... A> void printf(const char *f, A... a) { std::printf(f, a...); }
    int available() { return pending >= 0; }
    int read() { int c = pending; pending = -1; return c; }
};
inline FakeSerial Serial;
