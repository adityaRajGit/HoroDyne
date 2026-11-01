# Horodyne

A minimal, watchOS-style watch for the **Waveshare ESP32-S3-Touch-LCD-1.69**, in landscape
(280 × 240), running on a 1000 mAh LiPo. Apps use **Unica One**; the clock face uses **Valorax**
for the time and step number and **Space Mono** for small labels.

- Design and spec: `docs/superpowers/specs/2026-10-10-horodyne-watch-ui-design.md`
- Review page with the clickable prototype: https://claude.ai/artifact/1144rMJj79hpPYZEYEDxy5

## What's in this folder

| Path | What it is |
|---|---|
| `Horodyne/` | The sketch. Every file becomes one tab in the Arduino Cloud editor. |
| `horodyne_lvconf.zip` | LVGL's settings file (`lv_conf.h`) packaged as a library, because the Cloud editor has no other place to put it. |
| `horodyne_lvconf/` | The same library, unzipped. |

## Upload with the Arduino Cloud editor

1. **Import the settings library once.** Libraries → Import → choose `horodyne_lvconf.zip`.
2. **Add these libraries** from the library manager. These versions compiled cleanly:

   | Library | Version tested |
   |---|---|
   | lvgl | 9.6.0 |
   | LovyanGFX | 1.2.32 |
   | WiFiManager (tzapu) | 2.0.17 |
   | ArduinoJson | 7.4.3 |
   | SensorLib (Lewis He) | 0.5.0 |

3. **Create the sketch.** Make a new sketch called `Horodyne`, then add one tab for each file in
   `Horodyne/` with the same name and paste in its contents. That includes `Horodyne.ino`, every
   `.h`, `.cpp`, and the five `font_*.c` files. `UnicaOne-OFL.txt` is the font license and doesn't need a tab.
4. **Board settings**: board **ESP32S3 Dev Module**, ESP32 core **3.3.x**, and:

   | Setting | Value |
   |---|---|
   | Flash Size | 16MB (128Mb) |
   | PSRAM | OPI PSRAM |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
   | USB CDC On Boot | Enabled |

5. **Before connecting the battery**, check the plug. Generic 802540 cells often have a JST-PH 2.0
   plug, which may not fit this board's socket or may have reversed polarity. Red must go to `+`.
6. Upload. The first boot shows `--:--` and **SET UP WI-FI**: open Settings → Wi-Fi Setup, join the
   network **HORODYNE-SETUP** from your phone, pick your Wi-Fi and type your city.

## Build from this PC (arduino-cli)

Everything the build needs lives in `.tools/` (gitignored), so nothing depends on
`AppData\Local\Arduino15`. The ESP32 core 3.3.12-cn comes from Espressif's CN index, the five
libraries are copied into `.tools/user`, and the cache sits in `.tools/build-cache`.

```
sh build.sh            # compile only
sh build.sh upload     # compile and flash (PORT=COM5 sh build.sh upload for another port)
```

`build.sh` bumps the build number and time stamp in `Horodyne/build_info.h` first, so the boot
splash and Settings > About show `V0.2.<build>` and when it was built. It runs the same
arduino-cli command as before; keep its paths as they are, because a different path spelling
makes arduino-cli rebuild every library (~45 min).

- Keep `--jobs 2`: more runs this PC out of memory.
- While it's plugged into a computer the watch stays reachable over USB, so you can upload any time. If the
  port still refuses, hold BOOT, tap RESET, release BOOT, and upload again.
- To flash a finished build without arduino-cli:
  `python -m esptool --chip esp32s3 -p COM3 -b 921600 write-flash 0x10000 .tools/build/Horodyne.ino.bin`.
- Serial log at 115200: boot lines (`horodyne: v…`, `imu:`, `rtc:`, `net:`), `wake: motion, …` on raise, and `bat: raw … V avg … V NN% …` every 10 s.

### PC tests

`sh test/run_tests.sh` builds `Horodyne/hal_battery.cpp` on the PC with stub Arduino headers
(`test/stubs/`) and runs `test/test_battery.cpp`. It uses GCC 14.1 from MSYS2 in `.tools/msys64`.

## Phase 0: checks only the hardware can answer

Set `DEBUG_NO_SLEEP 1` in `config.h` to keep USB serial alive on a phone charger too. On a
computer's USB the watch already stays awake. Open the serial monitor at 115200.

| Check | How | If it's wrong |
|---|---|---|
| Clock chip | Boot log prints `rtc: ticking` or `rtc: not ticking`. Settings shows CLOCK CHIP. | Nothing to change; the watch falls back to Wi-Fi time. |
| Touch direction | Tap the top-left complication; it should open Activity. | Set `TOUCH_FLIP_X` / `TOUCH_FLIP_Y` in `hal_display.cpp`. |
| Screen position | All four corners of the face are visible, nothing clipped. | Adjust `offset_y` in `hal_display.cpp`. |
| Raise-to-wake axis | Open Level, lay the watch flat, read `imu ... z=` in the log. | If z ≈ -1.0, set `IMU_FACE_SIGN -1`. If x or y ≈ 1.0, change `IMU_FACE_AXIS`. |
| Raise-to-wake line | With `DEBUG_NO_SLEEP 0`, let the screen sleep, then lift the watch. | If it never wakes, change `IMU_WAKE_PIN` to `PIN1` in `hal_imu.cpp`. |
| Level direction | Tilt the right edge down: the bubble should move left. | `LEVEL_SWAP_XY`, `LEVEL_INV_X`, `LEVEL_INV_Y` in `config.h`. |
| Step counting | Boot log prints `imu: accel ok, pedometer ok, motion wake ok`. Walk 50 steps, then open Activity. | Nothing counts: the step engine is tuned for walking, not shaking by hand. Still zero: report the boot line. |
| Battery voltage | Compare Battery → volts with a multimeter on the cell. | Adjust `BAT_LOAD_COMP_V` or the divider value in `config.h`. |
| Sleep current | Meter in series with the battery while the screen is off. | The battery-life estimate on the review page assumes about 1.5 mA. |
| Clock drift | After a 6-hour sync the log prints `clock: drift +N s`. | Large drift with a dead RTC: shorten `SYNC_EVERY_MS`. |

## Notes

- Weather comes from Open-Meteo (no API key). HTTPS certificates aren't checked, so the worst a
  spoofed server could do is show the wrong weather.
- GitHub: on the phone setup page (Control Center > Wi-Fi) enter the GitHub username and a read-only
  token. Requests verify GitHub's certificate (`Horodyne/gh_ca.h`) because they carry the token.
  Data is fetched only when the app opens or refresh is tapped on the face. The token is stored
  unencrypted in NVS, so keep it read-only and revoke it if the watch is lost.
- Unica One and Space Mono are licensed under the SIL Open Font License (`Horodyne/*-OFL.txt`).
- Valorax (`.tools/fonts/valorax/Valorax-lg25V.otf`) is freeware for **non-commercial use only**.
  `Horodyne/font_vx_time.c` and `Horodyne/font_vx_num.c` are not in this repo, so the firmware won't
  build until you get the font from its authors and generate them, from inside `Horodyne/`:
  `lv_font_conv --font <Valorax.otf> --bpp 4 --format lvgl --lv-include lvgl.h --no-compress --size 64 --symbols 0123456789- -o font_vx_time.c`
  and the same with `--size 22 --symbols "0123456789,-AMP" -o font_vx_num.c`.
