#pragma once
// Settings saved in flash (NVS) so they survive power-off.
#include <Arduino.h>

struct Settings {
    uint8_t  brightness;   // 25, 50, 75 or 100 (percent)
    uint8_t  timeoutS;     // 5, 10, 15 or 30
    bool     raiseToWake;
    bool     h24;
    uint8_t  tzIndex;      // index into the time-zone list in svc_clock.cpp
    uint16_t stepGoal;
    bool     lowPower;
    char     city[40];
    float    lat, lon;     // looked up from the city name
    bool     hasLocation;
    char     ghUser[40];    // GitHub login
    char     ghToken[128];  // read-only token, typed on the setup page
};

extern Settings settings;

void settingsLoad();
void settingsSave();

// Cycle helpers used by Control Center and Settings.
void settingsNextBrightness();
void settingsNextTimeout();
void settingsNextStepGoal();
