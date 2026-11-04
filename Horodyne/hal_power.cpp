#include "hal_power.h"
#include "config.h"
#include "hal_battery.h"   // the 'b' serial command while polling on USB
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <esp_task_wdt.h>

static bool     ignoreUntilRelease = true;  // PWR is still held from switching the watch on
static bool     wasDown = false;
static uint32_t downAt = 0;
static bool     longFired = false;
static uint8_t  clicks = 0;
static uint32_t lastUpAt = 0;

void powerLatch() {
    pinMode(PIN_SYS_EN, OUTPUT);
    digitalWrite(PIN_SYS_EN, HIGH);
    gpio_hold_en((gpio_num_t)PIN_SYS_EN);   // stays HIGH through light sleep
    pinMode(PIN_PWR_KEY, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    gpio_hold_en((gpio_num_t)PIN_BUZZER);
}

bool powerKeyDown() { return digitalRead(PIN_PWR_KEY) == LOW; }

// Off: pin held LOW (an idle passive buzzer draws current otherwise), as powerLatch() leaves it.
void buzzerSet(bool on) {
    static bool isOn = false;
    if (on == isOn) return;
    isOn = on;
    if (on) {
        gpio_hold_dis((gpio_num_t)PIN_BUZZER);
        ledcAttach(PIN_BUZZER, BUZZER_HZ, 8);
        ledcWrite(PIN_BUZZER, 128);   // 50 % duty
    } else {
        ledcDetach(PIN_BUZZER);
        pinMode(PIN_BUZZER, OUTPUT);
        digitalWrite(PIN_BUZZER, LOW);
        gpio_hold_en((gpio_num_t)PIN_BUZZER);
    }
}

PwrEvent powerPoll() {
    uint32_t now = millis();
    bool down = powerKeyDown();

    if (ignoreUntilRelease) {
        if (!down) ignoreUntilRelease = false;
        return PWR_NONE;
    }
    if (down && !wasDown) { downAt = now; longFired = false; }
    if (down && !longFired && now - downAt >= LONG_PRESS_MS) {
        longFired = true; clicks = 0; wasDown = down;
        return PWR_LONG;
    }
    if (!down && wasDown && !longFired && now - downAt > 30) {   // 30 ms debounce
        clicks++; lastUpAt = now;
    }
    wasDown = down;

    if (clicks > 0 && !down && now - lastUpAt > DOUBLE_CLICK_MS) {
        PwrEvent e = clicks >= 2 ? PWR_DOUBLE : PWR_CLICK;
        clicks = 0;
        return e;
    }
    if (clicks >= 2 && !down) { clicks = 0; return PWR_DOUBLE; }
    return PWR_NONE;
}

// The wake that brought the screen on already used this press.
static void swallowPress() { ignoreUntilRelease = true; clicks = 0; }

WakeReason powerLightSleep(uint64_t timerUs, bool imuWake, bool stayAwake) {
    // The IMU wake-on-motion line toggles, so wake on whichever level it is NOT at now.
    int imuLevel = digitalRead(PIN_IMU_INT);

    // On a computer's USB, or while debugging: same wake rules, but polled, so USB stays
    // connected for uploads and the serial log. Battery life doesn't matter on USB power.
    if (stayAwake || DEBUG_NO_SLEEP || usb_serial_jtag_is_connected()) {
        uint32_t t0 = millis();
        while (true) {
            if (powerKeyDown()) { swallowPress(); return WAKE_PWR; }
            if (digitalRead(PIN_TP_INT) == LOW) return WAKE_TOUCH;
            if (imuWake && digitalRead(PIN_IMU_INT) != imuLevel) return WAKE_IMU;
            uint64_t elapsedUs = (uint64_t)(millis() - t0) * 1000ULL;
            if (timerUs && elapsedUs >= timerUs) return WAKE_TIMER;
            batterySerialPoll();
            if (!stayAwake && !DEBUG_NO_SLEEP && !usb_serial_jtag_is_connected()) {   // unplugged: really sleep
                if (timerUs) timerUs -= elapsedUs;
                break;
            }
            delay(20);
        }
    }

    gpio_wakeup_enable((gpio_num_t)PIN_TP_INT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)PIN_PWR_KEY, GPIO_INTR_LOW_LEVEL);
    if (imuWake) gpio_wakeup_enable((gpio_num_t)PIN_IMU_INT, imuLevel ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    else         gpio_wakeup_disable((gpio_num_t)PIN_IMU_INT);
    esp_sleep_enable_gpio_wakeup();
    if (timerUs) esp_sleep_enable_timer_wakeup(timerUs);

    esp_light_sleep_start();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable((gpio_num_t)PIN_TP_INT);
    gpio_wakeup_disable((gpio_num_t)PIN_PWR_KEY);
    gpio_wakeup_disable((gpio_num_t)PIN_IMU_INT);

    if (cause == ESP_SLEEP_WAKEUP_TIMER) return WAKE_TIMER;
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        if (powerKeyDown()) { swallowPress(); return WAKE_PWR; }
        if (digitalRead(PIN_TP_INT) == LOW) return WAKE_TOUCH;
        if (imuWake && digitalRead(PIN_IMU_INT) != imuLevel) return WAKE_IMU;
    }
    return WAKE_OTHER;
}

void powerOff() {
    // On battery this cuts power and nothing after it runs.
    gpio_hold_dis((gpio_num_t)PIN_SYS_EN);
    digitalWrite(PIN_SYS_EN, LOW);
    delay(300);
    // Still running, so USB is powering the board. Sleep until PWR is held for 2 s, then reboot.
    esp_task_wdt_delete(NULL);   // this wait has no end for the watchdog to see
    while (true) {
        while (powerKeyDown()) delay(10);
        powerLightSleep(0, false);
        uint32_t t0 = millis();
        while (powerKeyDown()) {
            if (millis() - t0 >= LONG_PRESS_MS) ESP.restart();
            delay(10);
        }
    }
}
