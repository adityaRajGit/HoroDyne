#pragma once
// Wi-Fi: the 6-hourly sync (NTP time + weather), and the phone setup page.
#include <Arduino.h>
#include <time.h>

struct Weather {
    bool    valid;
    int     temp, hi, lo;
    int     code;            // WMO weather code from Open-Meteo
    bool    isDay;
    int     hTemp[4], hCode[4];
    bool    hDay[4];
    char    hLabel[4][6];    // "13:00"
    time_t  fetchedAt;
};

extern Weather weather;

bool     netHasCredentials();
bool     netSync();                  // connect, NTP, weather, radio off. Blocks up to ~20 s.
bool     netGithubSync();             // connect, fetch GitHub, radio off. Blocks up to ~30 s.
uint32_t netLastSyncMs();             // millis() of the last attempt, 0 if never
bool     netLastSyncOk();

// Wi-Fi setup page: the watch becomes the network "HORODYNE-SETUP"; join it from a
// phone, pick your Wi-Fi and type the weather city and GitHub login. Non-blocking: call netPortalProcess() in loop.
void netPortalStart();
int  netPortalProcess();   // 0 = still open, 1 = saved and synced, -1 = timed out or failed
void netPortalStop();

const char *weatherCondition(int code);   // "CLEAR", "CLOUDY", ...
