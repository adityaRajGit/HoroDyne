# Raise to wake — design spec

Date: 2026-11-11 · Status: implemented in build 43, verified on the watch · Code: `hal_imu.cpp`, `hal_power.cpp`, `sleepUntilWake()` in `Horodyne.ino`, `config.h` (Raise to wake) · Related: [step counting](2026-11-11-step-counting-design.md), which shares the accelerometer and its FIFO

## 1. Behaviour

While the screen is off, raising the wrist so the screen faces you turns the screen on, with no button or tap.

- Switched on and off by **Raise to wake** in Settings and in Control Center (`settings.raiseToWake`, on by default).
- Movement that doesn't end with the screen facing up (walking, putting the watch down) leaves the screen off.
- After a rejected movement, motion is ignored for a short cooldown so that continuous movement doesn't keep the chip awake.

## 2. Hardware facts (measured 2026-11-11)

| Fact | Evidence |
|---|---|
| GPIO38 is the QMI8658's **INT1**. INT2 does not reach it. | With motion routed to INT2 the chip set the any-motion flag (STATUS1 = 0x20) while GPIO38 stayed at 1. As soon as INT1 was enabled, GPIO38 went to 0. Waveshare's documents name both pins. |
| With INT1 disabled, GPIO38 floats and reads 1 through the ESP32's pull-up. With INT1 enabled, it reads 0 at rest. | `imu` readings, builds 40–43 |
| The any-motion flag can be latched (STATUS1 = 0x20) while GPIO38 reads 0, so INT1 is not a level held high until STATUS1 is read. | `imu` reading at 03:00:28, build 43 |
| Setting up the FIFO (`configFifo`) after the motion engine leaves the motion engine silent. | STATUS1 stayed 0x00 through a minute of hard shaking with the boot setup. Redoing only the motion setup, at the same 31.25 Hz, made it fire straight away. |
| The motion engine works at the step counter's 31.25 Hz. No higher rate is needed. | `imucfg 28 2` test, build 41 |

## 3. How it works

```
awake ──timeout──▶ sleepUntilWake()
                     │ read STATUS1 (clears the latched flag), note GPIO38's level
                     ▼
                   light sleep: wake on GPIO38 ≠ that level, touch, PWR or timer
                     │ GPIO38 changed (WAKE_IMU)
                     ▼
                   facing up?  checked every 50 ms for up to RAISE_SETTLE_MS
                     ├─ yes ─▶ screen on
                     └─ no ──▶ ignore motion for RAISE_COOLDOWN_MS, back to sleep
```

1. **Sensor setup** (`imuBegin`): accelerometer at 31.25 Hz and ±4 g, then the step FIFO (stream mode, 128 samples), then the gyro off, then `startMotion()`. `startMotion()` sets up the any-motion engine on all three axes and routes it to INT1 (CTRL1 INT1 enable, CTRL8 bit 6 = 1, bit 1 = any-motion on). Expected registers: CTRL1 = 0x68, CTRL8 = 0xC2.
2. **Detection** (in the chip): any-motion fires when the change between two consecutive samples on any axis is above `WOM_THRESHOLD_MG` for 4 samples in a row (about 130 ms).
3. **Wake** (`powerLightSleep`): the sleep code doesn't assume a polarity. It wakes when GPIO38 moves away from the level it had on entering sleep, so it works whether INT1 toggles or pulses.
4. **Accept or reject** (`sleepUntilWake`): `imuFacingUp()` needs the screen-normal axis (z, sign −1) to read at least `RAISE_MIN_G`. The wrist is often still turning when motion fires, so the check repeats for up to `RAISE_SETTLE_MS` before rejecting. If the accelerometer can't be read, it wakes rather than miss a glance.

## 4. Coding decisions

| # | Decision | Why |
|---|---|---|
| D1 | Use the chip's **any-motion** engine, not its wake-on-motion (WoM) mode. | WoM mode stops the accelerometer data that the step counter needs (found during bring-up). Any-motion runs alongside the FIFO. |
| D2 | Route motion to **INT1** (`IMU_WAKE_PIN`). | Measured, see §2. INT2 was the original guess and never woke the watch. |
| D3 | Set up motion **after** the FIFO, in one function, `startMotion()`. Run it again after every FIFO restart in `imuReadSamples()`. | FIFO setup silences the motion engine (§2). A restart that left motion off would quietly disable raise to wake until the next reboot. Setting up motion afterwards doesn't disturb the FIFO, and steps kept counting after it. |
| D4 | Keep the accelerometer at **31.25 Hz**. | The 128-sample FIFO must cover the 3 s between step reads while asleep (`STEP_POLL_MS`). At 31.25 Hz it holds 4.1 s; at 62.5 Hz only 2 s. The motion engine works at 31.25 Hz, so there's no reason to spend more. |
| D5 | Threshold **62.5 mg**, window 4 samples. | The threshold compares sample to sample, so at 31 Hz a normal raise changes each axis by only about 50–100 mg per sample. At 200 mg, raises woke the watch once in two minutes. At 62.5 mg, 6 of 6 raises woke it. The chip stores the threshold in steps of 31.25 mg, so 62.5 is exactly two steps. |
| D6 | Decide on **orientation**, not on the motion alone. Wait up to 400 ms for it to settle, then cool down for 1.5 s after a rejection. | Any-motion also fires while walking or putting the watch down. The orientation check keeps those from lighting the screen, and the cooldown stops a walk from keeping the chip awake. |
| D7 | Don't trust the INT1 line's polarity. Wake on any change from the level at sleep entry, and read STATUS1 before sleeping. | Whether INT1 toggles or pulses isn't documented for this mode (§2). Reading STATUS1 clears a latched event, so an old event can't wake the watch at once. |
| D8 | Keep a serial `imu` command. | It prints the axes, GPIO38, CTRL1, CTRL8 and STATUS1 (reading STATUS1 clears it). That is enough to tell a wiring fault from a configuration fault without new firmware. |

## 5. Tuning values (`config.h`)

| Name | Value | Raise it if… | Lower it if… |
|---|---|---|---|
| `WOM_THRESHOLD_MG` | 62.5 | it wakes from small knocks or walking too often | a normal raise doesn't wake it |
| `RAISE_MIN_G` | 0.55 (about 57° from flat) | it lights up when not being looked at | a steep viewing angle is rejected |
| `RAISE_SETTLE_MS` | 400 | — | battery drain from motion wakes shows up |
| `RAISE_COOLDOWN_MS` | 1500 | walking keeps the chip busy | a second raise right after a rejection is missed |
| `IMU_FACE_AXIS`, `IMU_FACE_SIGN` | 2, −1 | — | only for a different mounting (flat and face up should read z ≈ −0.97 g) |

## 6. Test record (2026-11-11, watch on USB)

| Build | Setup | Result |
|---|---|---|
| 40 | INT2, motion set up before the FIFO, 200 mg | Shaken for 10 s: STATUS1 0x00 throughout, GPIO38 stuck at 1 |
| 41 | Live re-setup (`imucfg`, since removed) | Motion only after re-setup, at any rate. GPIO38 follows only INT1. |
| 42 | INT1, motion after the FIFO, 200 mg | One motion wake in about 2 minutes of raises |
| 43 | INT1, motion after the FIFO, 62.5 mg | 6 raises woke it, 2 screen-away moves were ignored, 49 s at rest with no wakes |

**Verifying a change:** with raise to wake on, put the watch flat and face up and wait for the screen to go off. Raise it five times. Each raise should log `wake: motion, facing up`, and putting it down should log at most `not facing up, back to sleep`. Then leave it still for a minute: there should be no `wake:` lines.

## 7. Open questions and risks

- **Battery cost while walking.** Every step can fire a motion wake that is then rejected (up to 0.4 s awake, then a 1.5 s cooldown). Not measured. If it shows up, raise the threshold first, then shorten the settle time.
- **Steep viewing angles.** Holding the watch more upright than about 57° from flat is rejected. Lower `RAISE_MIN_G` if real use needs it.
- **INT1 pulse length.** If INT1 pulses rather than toggles, a very short pulse could be missed while the code is polling instead of sleeping. Real light sleep wakes reliably (build 43 test).
- **USB debugging.** On a computer's USB the watch is meant to poll instead of sleeping, but it really sleeps and stops reading serial until it wakes. `usb_serial_jtag_is_connected()` seems unreliable here. This affects testing only.
