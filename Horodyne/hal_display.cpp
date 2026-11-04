#include "hal_display.h"
#include "config.h"
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <esp_heap_caps.h>

// ----- Panel ----------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
public:
    LGFX() {
        {
            auto c = _bus.config();
            c.spi_host = SPI2_HOST; c.spi_mode = 0;
            c.freq_write = LCD_SPI_HZ; c.freq_read = 16000000;
            c.spi_3wire = true; c.use_lock = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk = PIN_LCD_SCLK; c.pin_mosi = PIN_LCD_MOSI;
            c.pin_miso = -1; c.pin_dc = PIN_LCD_DC;
            _bus.config(c); _panel.setBus(&_bus);
        }
        {
            auto c = _panel.config();
            c.pin_cs = PIN_LCD_CS; c.pin_rst = PIN_LCD_RST; c.pin_busy = -1;
            c.panel_width = 240; c.panel_height = 280;      // native portrait size
            c.memory_width = 240; c.memory_height = 320;    // 240x320 RAM behind 240x280 glass
            c.offset_x = 0; c.offset_y = 20;
            c.readable = false; c.invert = true; c.rgb_order = false;
            c.bus_shared = false;
            _panel.config(c);
        }
        {
            auto c = _light.config();
            c.pin_bl = PIN_LCD_BL; c.invert = false;
            c.freq = 44100; c.pwm_channel = 7;
            _light.config(c); _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};
static LGFX tft;

// ----- Touch (CST816T, read through Wire) -----------------------------------
// The controller reports portrait coordinates (240 x 280). With setRotation(1)
// they map to landscape as below. If taps land mirrored, flip TOUCH_FLIP_X/Y.
#define TOUCH_FLIP_X 0
#define TOUCH_FLIP_Y 0
static const uint8_t ADDR_TOUCH = 0x15;

static volatile uint32_t lastTouchMs = 0;
static bool ignoreTouch = false;

static void touchBegin() {
    pinMode(PIN_TP_INT, INPUT_PULLUP);
    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, LOW);  delay(10);
    digitalWrite(PIN_TP_RST, HIGH); delay(60);
    // Auto-sleep stays on: in its low-power mode the chip still pulses INT on a
    // touch, which is what wakes the ESP32 from light sleep.
}

static bool touchRead(int16_t &x, int16_t &y) {
    Wire.beginTransmission(ADDR_TOUCH);
    Wire.write(0x02);   // FingerNum, then XposH, XposL, YposH, YposL
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom((int)ADDR_TOUCH, 5) != 5) return false;
    uint8_t n = Wire.read();
    uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
    if (n == 0) return false;
    int16_t nx = ((xh & 0x0F) << 8) | xl;   // 0..239 portrait
    int16_t ny = ((yh & 0x0F) << 8) | yl;   // 0..279 portrait
    x = ny;                 // rotation 1: portrait y runs along landscape x
    y = 239 - nx;
    if (TOUCH_FLIP_X) x = SCREEN_W - 1 - x;
    if (TOUCH_FLIP_Y) y = SCREEN_H - 1 - y;
    if (x < 0) x = 0; if (x >= SCREEN_W) x = SCREEN_W - 1;
    if (y < 0) y = 0; if (y >= SCREEN_H) y = SCREEN_H - 1;
    return true;
}

static void touchCb(lv_indev_t *, lv_indev_data_t *data) {
    int16_t x, y;
    bool down = touchRead(x, y);
    if (down) lastTouchMs = millis();
    if (ignoreTouch) {
        if (!down) ignoreTouch = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (down) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ----- LVGL -----------------------------------------------------------------
static const uint32_t BUF_LINES = 40;

// DMA: LVGL renders the next band into the other buffer while this one is sent.
// pushImageDMA waits for the previous transfer first, so a buffer is never reused mid-send.
static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    if (tft.getStartCount() == 0) tft.startWrite();   // keep the SPI bus between bands
    tft.pushImageDMA(area->x1, area->y1, w, h, (const lgfx::rgb565_t *)px);
    lv_display_flush_ready(disp);
}

void displayBegin() {
    tft.init();
    tft.setRotation(1);       // landscape 280 x 240
    tft.setBrightness(0);
    tft.fillScreen(0x0000);

    touchBegin();

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
    size_t bytes = SCREEN_W * BUF_LINES * 2;
    void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b1) { Serial.println("display: no memory for the draw buffer"); delay(100); ESP.restart(); }
    lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flushCb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchCb);
}

void displaySetBacklight(uint8_t percent) {
    tft.setBrightness((uint16_t)percent * 255 / 100);
}

void displaySleep() {
    while (tft.getStartCount()) tft.endWrite();   // finishes the last DMA, releases the bus
    tft.setBrightness(0);
    tft.sleep();
}

void displayWake() {
    tft.wakeup();
}

uint32_t displayLastTouchMs() { return lastTouchMs; }
void     displayIgnoreTouchUntilRelease() { ignoreTouch = true; }
