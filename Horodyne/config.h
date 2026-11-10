#pragma once
// ==========================================================================
// Horodyne -- board pins, tuning constants and defaults.
// Board: Waveshare ESP32-S3-Touch-LCD-1.69, used in landscape (280 x 240).
// Pins from Waveshare's docs: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69
// ==========================================================================
#include <Arduino.h>

#define HORODYNE_VERSION "0.2"   // shown as V0.2.<build number>; bump by hand for big changes

// ----- Display (ST7789V2) -------------------------------------------------
#define PIN_LCD_MOSI 7
#define PIN_LCD_SCLK 6
#define PIN_LCD_CS   5
#define PIN_LCD_DC   4
#define PIN_LCD_RST  8
#define PIN_LCD_BL   15
#define SCREEN_W     280   // landscape
#define SCREEN_H     240
#define LCD_SPI_HZ   80000000   // ST7789 write clock; if the screen shows noise, go back to 40 MHz

// ----- Shared I2C bus: touch 0x15, RTC 0x51, IMU 0x6B ---------------------
#define PIN_I2C_SDA  11
#define PIN_I2C_SCL  10
#define PIN_TP_INT   14
#define PIN_TP_RST   13
#define PIN_IMU_INT  38
#define PIN_RTC_INT  39
#define ADDR_RTC     0x51
#define ADDR_IMU     0x6B

// ----- Power ------------------------------------------------------------
#define PIN_PWR_KEY  40    // SYS_OUT: pulled up, LOW while PWR is pressed
#define PIN_SYS_EN   41    // HIGH keeps the board powered from the battery
#define PIN_BAT_ADC  1     // battery through a 200k / 100k divider
#define BAT_DIVIDER  3.0f
#define PIN_BUZZER   42    // passive buzzer: hold LOW or it draws current (Waveshare FAQ)

// ----- Timing ------------------------------------------------------------
#define DIM_MS            2000UL                 // dim phase before the screen turns off
#define LEVEL_TIMEOUT_MS  60000UL                // Level keeps the screen on longer
#define BACK_TO_FACE_MS   (2UL * 60UL * 1000UL)  // asleep longer than this -> wake on the face
#define SYNC_EVERY_MS     (6UL * 3600UL * 1000UL)
#define SYNC_EVERY_LOW_MS (12UL * 3600UL * 1000UL)
#define LONG_PRESS_MS     2000UL
#define DOUBLE_CLICK_MS   300UL
#define WDT_TIMEOUT_S     90UL                     // longer than the slowest sync, or a slow network reboots the watch

// ----- Wi-Fi -------------------------------------------------------------
// Transmit power in 0.25 dBm steps. At the driver's default of 20 dBm this board's signal can't be
// decoded even from half a metre away (no HORODYNE-SETUP, no joining the router); 13 dBm was heard
// at full strength in every scan. Try other values over serial with "txp <8..84>" and "wstat".
#define WIFI_TX_QDBM      52

// ----- Raise to wake -----------------------------------------------------
// Which accelerometer axis points out of the screen, and its sign when the
// screen faces up. Measured 2026-10-13: flat, face up, Level logs "imu z=-0.97".
#define IMU_FACE_AXIS      2      // 0 = x, 1 = y, 2 = z
#define IMU_FACE_SIGN      -1
#define RAISE_MIN_G        0.55f  // facing-up component needed to accept a wake
// Wake-on-motion sensitivity: the change between two samples (31 Hz) that has to hold for 4 samples,
// in steps of 31.25 mg. 200 only fired on hard shaking (one wake in two minutes of raises, 2026-11-11).
#define WOM_THRESHOLD_MG   62.5f
#define RAISE_COOLDOWN_MS  1500UL // ignore motion this long after a rejected wake
#define RAISE_SETTLE_MS    400UL  // keep checking this long after motion starts: the wrist is still turning

// ----- Steps: counted in software from the IMU FIFO (svc_activity.cpp) ------
// The QMI8658's own pedometer never counts on this board (four walk tests, 2026-10-26).
#define IMU_ODR_HZ         31     // accel samples per second into the FIFO (128 = ~4 s)
#define STEP_POLL_MS       3000   // the sleeping watch drains the FIFO this often (before it overflows)
// Too few steps: lower STEP_PEAK_G. Arm waving or double counts: raise STEP_PEAK_G or STEP_MIN_GAP_S,
// or raise STEP_ENTRY (steps in a row needed before any of them count).
#define STEP_PEAK_G        0.06f  // swing above (and below) the running average: wrist steps measure ~0.08-0.15 g
#define STEP_MIN_GAP_S     0.30f  // fastest cadence: 200 steps/min
#define STEP_MAX_GAP_S     2.0f   // longer without a step ends the walk
#define STEP_ENTRY         6

// ----- Level app: map IMU axes onto the landscape screen (check in Phase 0) --
// Tilt the right edge down: the bubble should move left (toward the high side).
#define LEVEL_SWAP_XY 0
#define LEVEL_INV_X   0
#define LEVEL_INV_Y   0

// ----- Battery -----------------------------------------------------------
// PL432224, 3.87 V, 1.08 Wh. Used as a 400 mAh cell; the label works out to about 280 mAh.
#define BAT_CAPACITY_MAH   400
#define BAT_LOAD_COMP_V    0.120f // added to readings taken with the screen on (measured sag 100-150 mV)
#define BAT_WARN_PCT       20
#define BAT_LOWPOWER_PCT   10
#define BAT_SHUTDOWN_PCT   3
#define BAT_RADIO_MIN_PCT  10     // no Wi-Fi below this: its current peaks can brown out the cell. Tuning knob
#define BAT_CHG_STEP_V     0.080f // instant jump/drop that means the cable went in/out
#define BAT_CHG_OFFSET_V   0.300f // charge current lifts the reading this much (until a jump or an unplug measures it; 0.35 V measured 2026-10-26)
#define BAT_CV_VOLTS       4.15f  // at or above this on the charger: constant-voltage phase, % creeps instead
#define BAT_CV_CREEP_MS    90000  // +1 % per 90 s in that phase (~70 min for the last 45 %)
#define BAT_RADIO_SETTLE_MS 3000  // cell recovery after Wi-Fi turns off before readings count again
#define BAT_CLIMB_MS       600000 // resting readings must stay higher this long before the percent rises

// ----- Timer alarm: passive buzzer on PIN_BUZZER ---------------------------
#define BUZZER_HZ     2700        // most small passive buzzers are loudest near 2.7 kHz
#define TIMER_RING_MS 30000UL     // the alarm beeps this long unless dismissed

// ----- Debug -------------------------------------------------------------
// Light sleep disconnects USB serial. Set to 1 while debugging over USB:
// the screen still turns off, but the chip stays awake.
#define DEBUG_NO_SLEEP 0

// ----- Defaults (Settings can change these) -------------------------------
#define DEF_BRIGHTNESS   75   // percent
#define DEF_TIMEOUT_S    10
#define DEF_STEP_GOAL    8000
#define DEF_TZ_INDEX     0    // IST, see svc_clock.cpp
#define DEF_CITY         ""

// ----- Watch colors (RGB888; LVGL converts to RGB565) -------------------
#define COL_BG        0x000000
#define COL_TEXT      0xFFFFFF
#define COL_SOFT      0xC7C7CC
#define COL_GREY      0x8E8E93
#define COL_CARD      0x1C1C1E
#define COL_FILL      0x2C2C2E
#define COL_LINE      0x3A3A3C
#define COL_ACTIVITY  0x30D158
#define COL_WEATHER   0x64D2FF
#define COL_BATTERY   0xFFD60A
#define COL_LEVEL     0xFF9F0A
#define COL_WARN      0xFF453A
#define COL_CHARGE    0x30D158   // green while on the charger
#define COL_TIMER     0xFF375F
#define COL_STOPWATCH 0xBF5AF2
#define COL_MERGED    0xA371F7   // GitHub: merged pull requests
#define COL_FACE      0xFF5A1F   // clock face accent: steps pill and timeline card
