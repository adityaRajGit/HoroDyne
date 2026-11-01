#pragma once
// ST7789 panel (LovyanGFX), LVGL setup, and CST816T touch.
#include <Arduino.h>

void     displayBegin();                 // panel, LVGL, touch input. Backlight stays off.
void     displaySetBacklight(uint8_t percent);
void     displaySleep();                 // backlight off + panel sleep (keeps the image)
void     displayWake();
uint32_t displayLastTouchMs();           // millis() of the last touch, 0 if none
void     displayIgnoreTouchUntilRelease(); // the touch that woke the screen shouldn't press anything
