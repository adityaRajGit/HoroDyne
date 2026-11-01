#include "svc_settings.h"
#include "config.h"
#include <Preferences.h>

Settings settings;
static Preferences prefs;

void settingsLoad() {
    prefs.begin("horodyne", true);
    settings.brightness  = prefs.getUChar("bright", DEF_BRIGHTNESS);
    settings.timeoutS    = prefs.getUChar("timeout", DEF_TIMEOUT_S);
    settings.raiseToWake = prefs.getBool("raise", true);
    settings.h24         = prefs.getBool("h24v2", false);   // new key: 12-hour with AM/PM is the default now
    settings.tzIndex     = prefs.getUChar("tz", DEF_TZ_INDEX);
    settings.stepGoal    = prefs.getUShort("goal", DEF_STEP_GOAL);
    settings.lowPower    = prefs.getBool("lowpwr", false);
    settings.lat         = prefs.getFloat("lat", 0);
    settings.lon         = prefs.getFloat("lon", 0);
    settings.hasLocation = prefs.getBool("hasloc", false);
    String city = prefs.getString("city", DEF_CITY);
    strlcpy(settings.city, city.c_str(), sizeof(settings.city));
    strlcpy(settings.ghUser, prefs.getString("ghuser", "").c_str(), sizeof(settings.ghUser));
    strlcpy(settings.ghToken, prefs.getString("ghtoken", "").c_str(), sizeof(settings.ghToken));
    prefs.end();
}

void settingsSave() {
    prefs.begin("horodyne", false);
    prefs.putUChar("bright", settings.brightness);
    prefs.putUChar("timeout", settings.timeoutS);
    prefs.putBool("raise", settings.raiseToWake);
    prefs.putBool("h24v2", settings.h24);
    prefs.putUChar("tz", settings.tzIndex);
    prefs.putUShort("goal", settings.stepGoal);
    prefs.putBool("lowpwr", settings.lowPower);
    prefs.putFloat("lat", settings.lat);
    prefs.putFloat("lon", settings.lon);
    prefs.putBool("hasloc", settings.hasLocation);
    prefs.putString("city", settings.city);
    prefs.putString("ghuser", settings.ghUser);
    prefs.putString("ghtoken", settings.ghToken);
    prefs.end();
}

void settingsNextBrightness() {
    settings.brightness = settings.brightness >= 100 ? 25 : settings.brightness + 25;
    settingsSave();
}

void settingsNextTimeout() {
    static const uint8_t steps[] = { 5, 10, 15, 30 };
    uint8_t next = steps[0];
    for (uint8_t i = 0; i < 4; i++) {
        if (steps[i] == settings.timeoutS) { next = steps[(i + 1) % 4]; break; }
    }
    settings.timeoutS = next;
    settingsSave();
}

void settingsNextStepGoal() {
    settings.stepGoal = settings.stepGoal >= 12000 ? 4000 : settings.stepGoal + 2000;
    settingsSave();
}
