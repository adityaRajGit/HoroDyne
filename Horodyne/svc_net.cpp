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
#include <esp_heap_caps.h>

Weather weather = {};
static uint32_t lastSyncMs = 0;
static bool     lastSyncOk = false;
static uint8_t  failCount = 0;    // failed syncs in a row

static int8_t credsCached = -1;   // -1 = not checked yet
static int8_t txQuarterDbm = WIFI_TX_QDBM;   // see config.h; "txp" over serial changes it until reboot

// Call once the radio is on: the driver resets the power each time it starts.
static void applyTxPower() {
    WiFi.setTxPower((wifi_power_t)txQuarterDbm);
}

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
uint8_t  netFailCount()  { return failCount; }

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

// The one way onto Wi-Fi, so no caller can start the radio on a nearly empty cell.
static bool connect(uint32_t timeoutMs) {
    if (!batteryRadioOk()) return false;
    batteryRadio(true);
    WiFi.mode(WIFI_STA);
    applyTxPower();
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
    if (!connect(10000)) {
        radioOff();
        if (failCount < 255) failCount++;
        Serial.println("net: Wi-Fi connect failed");
        return false;
    }
    lastSyncOk = clockNtpSync();
    // Weather every sync, Low Power Mode too: Wi-Fi is already up, so it costs a few seconds.
    if (!settings.hasLocation) geocodeCity();
    Serial.printf("net: weather %s\n", fetchWeather() ? "ok" : settings.hasLocation ? "fetch failed" : "no city set");
    radioOff();
    failCount = lastSyncOk ? 0 : failCount < 255 ? failCount + 1 : 255;
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
static const char SETUP_SSID[] = "HORODYNE-SETUP";
static bool     portalSaved = false;
static bool     portalStarted = false;   // between netPortalStart() and netPortalStop()
static uint32_t portalStartMs = 0;

// The setup network is on the air: the portal runs and the AP broadcasts our name.
static bool portalOnAir() {
    return wm.getConfigPortalActive() && (WiFi.getMode() & WIFI_AP) && WiFi.softAPSSID() == SETUP_SSID;
}

static void logPortal(const char *what) {
    Serial.printf("net: setup network %s: mode %d, ssid '%s', channel %d, ip %s, tx %d, internal heap %u\n", what,
                  (int)WiFi.getMode(), WiFi.softAPSSID().c_str(), (int)WiFi.channel(), WiFi.softAPIP().toString().c_str(),
                  (int)WiFi.getTxPower(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

bool netPortalFailed() { return portalStarted && !portalOnAir(); }

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
    // In non-blocking mode startConfigPortal() returns false even when it works, so check the radio itself.
    wm.startConfigPortal(SETUP_SSID);
    applyTxPower();
    if (!portalOnAir()) {
        logPortal("did not start, retrying");   // once more, from the radio fully off
        if (wm.getConfigPortalActive()) wm.stopConfigPortal();
        WiFi.mode(WIFI_OFF);
        delay(200);
        wm.startConfigPortal(SETUP_SSID);
        applyTxPower();
    }
    portalStarted = true;
    logPortal(portalOnAir() ? "on air" : "FAILED");
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
        bool online = WiFi.status() == WL_CONNECTED || connect(10000);
        credsCached = online ? 1 : -1;   // not online: the saved network may still work later, so look again
        if (online) {
            lastSyncMs = millis();
            lastSyncOk = clockNtpSync();
            geocodeCity();
            fetchWeather();
        }
        netPortalStop();
        return online ? 1 : -1;
    }
    if (millis() - portalStartMs > 300000UL) { netPortalStop(); return -1; }
    return 0;
}

void netPortalStop() {
    // WiFiManager's own 300 s timeout can shut the portal first; stopping it twice crashes on its freed web server.
    if (wm.getConfigPortalActive()) wm.stopConfigPortal();
    portalStarted = false;
    radioOff();
}

// ----- radio checks over serial --------------------------------------------------------
void netDebugTxPower(int q) {
    if (q < 8 || q > 84) { Serial.println("net: txp takes 8..84 quarter-dBm (34 = 8.5 dBm, 84 = 21 dBm)"); return; }
    txQuarterDbm = q;
    bool on = WiFi.getMode() != WIFI_OFF;
    if (on) applyTxPower();
    Serial.printf("net: tx power set to %d, radio %s%s\n", q, on ? "on, now " : "off", on ? String((int)WiFi.getTxPower()).c_str() : "");
}

void netDebugScan() {
    bool wasOff = WiFi.getMode() == WIFI_OFF;
    if (wasOff) { batteryRadio(true); WiFi.mode(WIFI_STA); applyTxPower(); }
    int n = WiFi.scanNetworks();   // blocks ~2 s
    Serial.printf("net: scan found %d networks\n", n);
    for (int i = 0; i < n && i < 12; i++)
        Serial.printf("net:   %4d dBm  ch %2d  %s\n", (int)WiFi.RSSI(i), (int)WiFi.channel(i), WiFi.SSID(i).c_str());
    WiFi.scanDelete();
    if (wasOff) radioOff();
}

void netDebugStatus() {
    if (WiFi.getMode() == WIFI_OFF) { Serial.println("net: radio off"); return; }
    Serial.printf("net: radio mode %d, ap '%s', channel %d, tx %d, sta %s\n", (int)WiFi.getMode(), WiFi.softAPSSID().c_str(),
                  (int)WiFi.channel(), (int)WiFi.getTxPower(), WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
}
