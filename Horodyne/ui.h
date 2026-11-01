#pragma once
// Screens, navigation and overlays (LVGL).
#include <Arduino.h>
#include "hal_power.h"

enum ScreenId : uint8_t {
    SCR_FACE, SCR_STACK, SCR_CONTROL, SCR_GRID,
    SCR_ACTIVITY, SCR_WEATHER, SCR_BATTERY, SCR_LEVEL, SCR_TIMER, SCR_STOPWATCH, SCR_GITHUB, SCR_SETTINGS, SCR_ABOUT,
    SCR_WIFI, SCR_GH_PRS, SCR_GH_PR, SCR_GH_RUNS, SCR_GH_RUN, SCR_GH_COMMITS   // GitHub sub-screens
};

// Work the UI asks the main loop to do (it may block, so the UI doesn't).
enum UiRequest : uint8_t { REQ_NONE, REQ_SYNC, REQ_GH_SYNC, REQ_PORTAL_START, REQ_PORTAL_CANCEL, REQ_POWEROFF };

void      uiSplash();                  // name, version and developer while the rest boots
void      uiBegin();
ScreenId  uiCurrent();
void      uiTick();                    // once a second: refresh the visible screen
void      uiRefresh();                 // refresh now (after a sync, for example)
void      uiOnPwr(PwrEvent e);
void      uiOnWake(bool backToFace);
void      uiPortalDone(bool ok);
bool      uiPowerSheetOpen();
void      uiShowLowBattery();
void      uiShowChargeMe();            // the 3 % shutdown notice
void      uiRequest(UiRequest r);
UiRequest uiTakeRequest();
bool      uiSyncing();
void      uiSetSyncing(bool on);
uint8_t   uiBrightnessPercent();       // settings brightness, capped in Low Power Mode
void      uiOpenApp(ScreenId app, ScreenId backTo);   // swipe right / back returns to backTo
