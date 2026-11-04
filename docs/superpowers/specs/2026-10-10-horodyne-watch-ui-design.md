# Horodyne watch UI — design spec

Date: 2026-10-10 · Status: approved design (font change applied)

## 1. Goal

A minimalist, watchOS 10–style clock and menu for the Waveshare ESP32-S3-Touch-LCD-1.69,
used in landscape (280 × 240), set entirely in **Unica One**, running on a 400 mAh PL432224 LiPo.
Success: glanceable time, five small apps, correct battery percentage, and multi-week battery life.

## 2. Fixed constraints

| Item | Decision |
|---|---|
| Board | ESP32-S3-Touch-LCD-1.69, board 2 (touch works, PCF85063 RTC not working) |
| Display | ST7789V2, landscape 280 × 240, RGB565, rounded corners ≈ 34 px |
| Input | CST816T touch + PWR key (GPIO40); BOOT unused at runtime |
| Font | Unica One in apps. Changed 2026-10-26: the face uses Valorax for time and steps, Space Mono for small labels |
| Time | Wi-Fi NTP at boot and every 6 h; Wi-Fi off between syncs |
| Idle | Screen off + light sleep; wake on raise (IMU), touch, PWR, sync timer |
| UI stack | LVGL 9 on LovyanGFX |
| Battery | PL432224, treated as 400 mAh (label: 3.87 V, 1.08 Wh, which works out to about 280 mAh), JST lead (check plug type and polarity). Charge off-board: the board's charger is about 2 A |
| Toolchain | Arduino sketch, Arduino Cloud editor (see §11) |
| Defaults | Step goal 8,000 · 24-hour · timeout 10 s · time zone IST (+5:30) |
| Weather location | City typed on the Wi-Fi setup page (no IP geolocation) |

## 3. Pin map (from Waveshare docs)

| Signal | GPIO | Signal | GPIO |
|---|---|---|---|
| LCD MOSI / SCLK / CS / DC / RST / BL | 7 / 6 / 5 / 4 / 8 / 15 | I2C SDA / SCL | 11 / 10 |
| Touch INT / RST | 14 / 13 | IMU INT | 38 |
| RTC INT | 39 | Battery ADC (×3 divider) | 1 |
| PWR key in (SYS_OUT) | 40 | Power latch (SYS_EN) | 41 |
| Buzzer | 42 (unused in v1) | BOOT | 0 |

I2C addresses: touch 0x15, RTC 0x51, IMU 0x6B.

## 4. Screens

All text in Unica One; content kept ≥ 14 px from the sides; tap targets ≥ 52 px.

| Screen | Content |
|---|---|
| Clock face | Date 22 px top-left; time 104 px in fixed-width digit slots; 3 complications (steps ring, weather, battery) that open their apps |
| Smart Stack | Swipe up from face. One card (Activity, Weather, Battery), next card peeks; vertical swipes move; tap opens |
| Control Center | Swipe down from face. Brightness (25/50/75/100), Sync now, Timeout (5/10/15/30 s), Raise to wake, Low Power, Wi-Fi status |
| App grid | PWR from face. 3 + 2 honeycomb of 62 px circles with 15 px labels |
| Activity | 150 px ring to goal, steps, goal, distance (0.75 m/step), 12 two-hour bars |
| Weather | City header, temperature 72 px, condition, H/L, data age, next 4 time slots |
| Battery | Percentage 72 px, resting voltage, days left, bar, Low Power toggle |
| Level | Dial with bubble, X/Y degrees, green within ±1°, screen on 60 s |
| Settings | 52 px rows: brightness, timeout, raise to wake, 24-hour, time zone, step goal, Wi-Fi setup, weather city, about |
| Low battery | Shown once at 10 % |
| Power off | Hold PWR → slide/tap red knob; PWR cancels |

## 5. Navigation

- Face ↑ Smart Stack, Face ↓ Control Center, PWR on face → app grid.
- In an app: swipe right = back; PWR = face; PWR double-press = last app; hold PWR = power-off sheet.
- Woken after > 2 min asleep → face; shorter → previous screen.
- No left/right swipe on the face in v1 (reserved for more faces).

## 6. Visual system

Colors (hex → RGB565): Activity #30D158 → 0x368B · Weather #64D2FF → 0x669F · Battery #FFD60A → 0xFEA1 ·
Level #FF9F0A → 0xFCE1 · Secondary #8E8E93 → 0x8C72 · Warning #FF453A → 0xFA27 · Card #1C1C1E → 0x18E3 · Background #000000.

Fonts generated with `lv_font_conv` from Unica One (OFL), 4 bpp:

| Font | Size | Glyphs |
|---|---|---|
| `font_time` | 104 | `0-9 :` |
| `font_hero` | 72 | `0-9 % ° - .` |
| `font_value` | 40 | `0-9 , . K ° %` |
| `font_label` | 22 | printable ASCII + ° |
| `font_small` | 16 | printable ASCII + ° |

Icons: 2 px line icons drawn with LVGL primitives or a small icon font. Transitions ≈ 240 ms slides; apps zoom in from the grid; Low Power disables animation.

## 7. Power

States: Active → Dim (after timeout) → Sleep (2 s later) → Active (raise/touch/PWR); Sleep → Sync (6 h timer) → Sleep; Off via PWR hold or 3 % battery.

Sleep: backlight 0, panel `SLPIN`, touch in its own standby with INT armed, IMU in normal mode (125 Hz accel) with pedometer + any-motion running
(wake-on-motion mode was dropped: the QMI8658 stops counting steps in it),
ESP32-S3 light sleep with GPIO wake on 14, 38, 40 and timer wake for the next sync. Raise-to-wake is accepted only if the
screen faces up; otherwise back to sleep in < 50 ms, with a cooldown after rejected wakes.

Low Power Mode (manual, or automatic at 10 %): brightness ≤ 40 %, no animations, no weather fetch, NTP every 12 h.

## 8. Battery percentage

1. `analogReadMilliVolts(1)`, 32 samples, trimmed mean, × 3.
2. Primary sample taken right after wake, before the backlight turns on; screen-on readings get a load offset measured in Phase 0.
3. 21-point LiPo resting curve (5 % steps; 4.20 V = 100 %, 3.84 V = 50 %, 3.73 V = 20 %, 3.69 V = 10 %, 3.27 V = 0 %), linear interpolation.
4. EMA α = 0.2; displayed value never rises while discharging.
5. Charging inferred (no status pin): USB frames from a computer, or an instant jump > 80 mV between two readings
   taken the same way (both screen-on or both screen-off), or a rise > 30 mV in 2 min. While charging the measured
   jump is subtracted before the curve lookup; show +NN % / CHARGING, cap at 99 %, re-read on unplug.
   Charging itself is done by the board's charger chip in hardware; firmware cannot start or stop it.
6. 20 % → red complication · 10 % → Low Power Mode on · 3 % on three readings in a row → "charge" screen, then SYS_EN low.

## 9. Firmware structure

Three layers; UI reads services only, never hardware.

- `hal_*`: display (LovyanGFX landscape, backlight, panel sleep, LVGL flush), touch (CST816T), imu (QMI8658 accel, pedometer, any-motion wake), battery, power (SYS_EN, PWR events, light sleep).
- `svc_*`: clock (NTP, TZ, schedule, drift log), weather (Open-Meteo, cached), activity (daily steps, midnight reset, 2-h bins), settings (NVS).
- `ui_*`: theme (colors, fonts), nav (screen manager, gestures, back stack, PWR routing), one file per screen.
- Main sketch: power state machine and LVGL loop.

## 10. Build order

0. Board 2 bring-up: landscape display, touch, IMU, battery ADC, SYS_EN latch; measure sleep and active current; re-test RTC (clear STOP bit in reg 0x00); check LiPo curve.
1. Skeleton: theme, Unica One fonts, screen manager, clock face.
2. Services: NTP clock, battery, steps, settings.
3. Screens: stack, control center, grid, five apps, power-off.
4. Power: dim/sleep/wake, raise to wake, sync timer, Low Power Mode; measure a full day and tune.

## 11. Open decision

Arduino Cloud editor cannot place LVGL's `lv_conf.h` next to the LVGL library. Choice pending between:
desktop Arduino IDE 2, Cloud editor with a small custom `lv_conf` library imported as a ZIP, or dropping LVGL for LovyanGFX-only rendering.

## 12. Risks

- Clock drift without RTC (measure; adapt sync interval; try RTC STOP-bit fix).
- Battery connector type/polarity.
- Sleep current above estimate (measure first).
- Raise-to-wake false triggers (orientation check, cooldown, setting).
- Unica One legibility (≥ 16 px, capitals, letter-spacing; verify ° and %).
- Landscape panel offset (verify with corner test pattern).
