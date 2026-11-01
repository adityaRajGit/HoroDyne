#include "svc_net.h"
#include "svc_clock.h"
#include "svc_settings.h"
#include "svc_github.h"
#include "hal_battery.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

Weather weather = {};
static uint32_t lastSyncMs = 0;
static bool     lastSyncOk = false;

static int8_t credsCached = -1;   // -1 = not checked yet

bool netHasCredentials() {
    if (credsCached < 0) {
        // Reading the saved network needs the Wi-Fi driver running, so only do it once.
        WiFi.mode(WIFI_STA);
        wifi_config_t conf;
        credsCached = (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0] != 0) ? 1 : 0;
        WiFi.mode(WIFI_OFF);
    }
    return credsCached == 1;
}

uint32_t netLastSyncMs() { return lastSyncMs; }
bool     netLastSyncOk() { return lastSyncOk; }

const char *weatherCondition(int code) {
    if (code == 0) return "CLEAR";
    if (code <= 2) return "PARTLY CLOUDY";
    if (code == 3) return "CLOUDY";
    if (code == 45 || code == 48) return "FOG";
    if (code >= 51 && code <= 57) return "DRIZZLE";
    if (code >= 61 && code <= 67) return "RAIN";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "SNOW";
    if (code >= 80 && code <= 82) return "SHOWERS";
    if (code >= 95) return "STORM";
    return "--";
}

// Open-Meteo needs no API key. TLS certificates are not checked (setInsecure):
// the worst a spoofed server could do is show wrong weather.
static bool httpGetJson(const String &url, JsonDocument &doc, JsonDocument &filter) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.useHTTP10(true);   // no chunked replies: getStream() hands raw chunk headers to the parser
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        ok = !err;
    }
    http.end();
    return ok;
}

static String urlEncode(const char *s) {
    String out;
    for (; *s; s++) {
        char c = *s;
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out += c;
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); out += b; }
    }
    return out;
}

static bool geocodeCity() {
    if (!settings.city[0]) return false;
    JsonDocument filter;
    filter["results"][0]["latitude"] = true;
    filter["results"][0]["longitude"] = true;
    filter["results"][0]["name"] = true;
    JsonDocument doc;
    String url = String("https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=") + urlEncode(settings.city);
    if (!httpGetJson(url, doc, filter)) return false;
    JsonVariant r = doc["results"][0];
    if (r.isNull()) return false;
    settings.lat = r["latitude"] | 0.0f;
    settings.lon = r["longitude"] | 0.0f;
    settings.hasLocation = true;
    String name = r["name"] | settings.city;
    name.toUpperCase();
    strlcpy(settings.city, name.c_str(), sizeof(settings.city));
    settingsSave();
    return true;
}

static bool fetchWeather() {
    if (!settings.hasLocation) return false;
    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"] = true;
    filter["current"]["is_day"] = true;
    filter["daily"]["temperature_2m_max"] = true;
    filter["daily"]["temperature_2m_min"] = true;
    filter["hourly"]["time"] = true;
    filter["hourly"]["temperature_2m"] = true;
    filter["hourly"]["weather_code"] = true;
    filter["hourly"]["is_day"] = true;

    char url[320];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min"
             "&hourly=temperature_2m,weather_code,is_day&forecast_hours=13&forecast_days=1&timezone=auto",
             settings.lat, settings.lon);
    JsonDocument doc;
    if (!httpGetJson(url, doc, filter)) return false;
    if (doc["current"]["temperature_2m"].isNull()) return false;   // parsed, but not a forecast

    weather.temp = (int)lroundf(doc["current"]["temperature_2m"] | 0.0f);
    weather.code  = doc["current"]["weather_code"] | 0;
    weather.isDay = (doc["current"]["is_day"] | 1) == 1;
    weather.hi    = (int)lroundf(doc["daily"]["temperature_2m_max"][0] | 0.0f);
    weather.lo    = (int)lroundf(doc["daily"]["temperature_2m_min"][0] | 0.0f);
    for (int i = 0; i < 4; i++) {
        int k = (i + 1) * 3;   // 3, 6, 9 and 12 hours ahead
        weather.hTemp[i] = (int)lroundf(doc["hourly"]["temperature_2m"][k] | 0.0f);
        weather.hCode[i] = doc["hourly"]["weather_code"][k] | 0;
        weather.hDay[i]  = (doc["hourly"]["is_day"][k] | 1) == 1;
        const char *t = doc["hourly"]["time"][k] | "";   // "2026-10-10T22:00"
        strlcpy(weather.hLabel[i], strlen(t) >= 16 ? t + 11 : "--:--", sizeof(weather.hLabel[i]));
    }
    weather.fetchedAt = time(nullptr);
    weather.valid = true;
    return true;
}

static bool connect(uint32_t timeoutMs) {
    batteryRadio(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin();   // credentials saved by the setup page
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
    return WiFi.status() == WL_CONNECTED;
}

static void radioOff() {
    WiFi.disconnect(true, false);   // keep the saved credentials
    WiFi.mode(WIFI_OFF);
    batteryRadio(false);
}

bool netSync() {
    lastSyncMs = millis();
    lastSyncOk = false;
    if (!connect(10000)) { radioOff(); Serial.println("net: Wi-Fi connect failed"); return false; }
    lastSyncOk = clockNtpSync();
    // Weather every sync, Low Power Mode too: Wi-Fi is already up, so it costs a few seconds.
    if (!settings.hasLocation) geocodeCity();
    Serial.printf("net: weather %s\n", fetchWeather() ? "ok" : settings.hasLocation ? "fetch failed" : "no city set");
    radioOff();
    Serial.printf("net: sync %s\n", lastSyncOk ? "ok" : "failed");
    return lastSyncOk;
}

// Only runs when the GitHub app opens or refresh is tapped: Wi-Fi stays off otherwise.
bool netGithubSync() {
    if (!connect(10000)) { radioOff(); gh.error = GH_NO_WIFI; Serial.println("github: Wi-Fi connect failed"); return false; }
    if (!clockValid()) clockNtpSync();   // certificates can't be checked without the date
    GhError e = clockValid() ? ghFetch() : GH_FETCH_FAILED;
    gh.error = e;
    radioOff();
    Serial.printf("github: sync %s\n", e == GH_OK ? "ok" : "failed");
    return e == GH_OK;
}

// ----- setup page ------------------------------------------------------------
static WiFiManager wm;
static WiFiManagerParameter cityParam("city", "City for weather", "", 39);
static WiFiManagerParameter ghUserParam("ghuser", "GitHub username", "", 39);
// Never echoed back to the page; left blank, the saved token stays.
static WiFiManagerParameter ghTokenParam("ghtoken", "GitHub read-only token (blank keeps the saved one)", "", 127, "type=\"password\"");
static bool     portalSaved = false;
static uint32_t portalStartMs = 0;

void netPortalStart() {
    cityParam.setValue(settings.city, 39);
    static bool paramAdded = false;
    ghUserParam.setValue(settings.ghUser, 39);
    if (!paramAdded) { wm.addParameter(&cityParam); wm.addParameter(&ghUserParam); wm.addParameter(&ghTokenParam); paramAdded = true; }
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(300);
    wm.setSaveConfigCallback([]() { portalSaved = true; });
    portalSaved = false;
    portalStartMs = millis();
    batteryRadio(true);
    wm.startConfigPortal("HORODYNE-SETUP");
}

int netPortalProcess() {
    bool connected = wm.process();
    if (connected || portalSaved) {
        const char *city = cityParam.getValue();
        if (city && city[0] && strcmp(city, settings.city) != 0) {
            strlcpy(settings.city, city, sizeof(settings.city));
            settings.hasLocation = false;   // look the new city up again
            settingsSave();
        }
        const char *user = ghUserParam.getValue(), *token = ghTokenParam.getValue();
        if (user && strcmp(user, settings.ghUser) != 0 && (!user[0] || ghValidLogin(user))) {
            strlcpy(settings.ghUser, user, sizeof(settings.ghUser));
            settingsSave();
        }
        if (token && token[0]) {
            strlcpy(settings.ghToken, token, sizeof(settings.ghToken));
            settingsSave();
        }
        if (WiFi.status() == WL_CONNECTED || connect(10000)) {
            credsCached = 1;
            lastSyncMs = millis();
            lastSyncOk = clockNtpSync();
            geocodeCity();
            fetchWeather();
        }
        netPortalStop();
        return 1;
    }
    if (millis() - portalStartMs > 300000UL) { netPortalStop(); return -1; }
    return 0;
}

void netPortalStop() {
    wm.stopConfigPortal();
    radioOff();
}
