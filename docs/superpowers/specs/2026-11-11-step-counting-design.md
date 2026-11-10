# Step counting — design spec

Date: 2026-11-11 · Status: implemented, build 44 counted a short walk on battery · Code: `svc_activity.cpp`, `imuReadSamples()` in `hal_imu.cpp`, `stepsSave()` / `stepsRestore()` / `sleepUntilWake()` in `Horodyne.ino`, `config.h` (Steps)

## 1. Behaviour

- Today's steps are shown on the face's step ring, in the Activity app and on the Smart Stack card. Distance is steps × 0.75 m. There are 12 two-hour bars.
- The count goes to zero at local midnight. It does **not** go to zero when the clock is corrected or the time zone changes.
- The count survives a reboot, up to the last save (at most 10 minutes old).
- Steps keep being counted while the screen is off, during Wi-Fi syncs, and while raise to wake is waking and rejecting the chip during a walk.

## 2. Why the counting is done in software

The QMI8658 has its own pedometer, but it never counted on this board: four walk tests on 2026-10-26, and Waveshare issue #109 reports the same. So the chip only buffers accelerometer samples, and the ESP32 finds the steps in them.

## 3. Data path

```
QMI8658 accel, 31.25 Hz, ±4 g
  └─▶ FIFO, stream mode, 128 samples (≈ 4.1 s, oldest dropped when full)
        └─▶ imuReadSamples(): |a| in g for each buffered sample
              └─▶ stepFeed(): the detector (§4)
                    └─▶ activity.steps and activity.bins[hour / 2]
```

The FIFO is drained by `activityUpdate()`:

| When | How often |
|---|---|
| Awake | every 1 s (main loop tick) |
| Asleep | every `STEP_POLL_MS` = 3 s, on a timer wake |
| Asleep, after a rejected raise | at the end of the 1.5 s cooldown, which is also a timer wake |
| On waking, after a Wi-Fi job, at boot | once |

The longest gap is 3 s. That is under the FIFO's 4.1 s, so no samples are lost.

## 4. Detector (`stepFeed`)

1. Keep a running average of |a| (`avgG += (g − avgG) × 0.05`, about 0.6 s). It follows gravity and slow changes of posture.
2. A **peak** is a sample more than `STEP_PEAK_G` above the average, at least `STEP_MIN_GAP_S` after the previous peak. After a peak, the detector waits until the signal has swung `STEP_PEAK_G` *below* the average before it accepts another one.
3. A **walk** is a run of peaks, each within `STEP_MAX_GAP_S` of the last. Nothing counts until `STEP_ENTRY` peaks have come in a row. Then those are counted all at once, and every later peak in the run counts as it happens. A single wave of the hand counts nothing.
4. Steps go into the two-hour bin of the previous update. The sleeping watch wakes just after each 2-hour boundary, so steps read in a new block were taken in the old one.

## 5. Day, clock and saving

- **Day number** = year × 1000 + day of year (`clockDayNumber()`). Before the clock is known, steps go to the total only, with no bins.
- **Midnight**: when the day number changes, the counts and bins are cleared. Steps read in that update are dropped, because they were taken before midnight. The 2-hourly wake keeps that batch small.
- **Clock jumps are not midnights.** NTP sync, re-reading the RTC (a jump of more than 60 s) and changing the time zone set a flag. `clockTakeJump()` hands that flag to `activityUpdate()`, which then accepts the new day without clearing.
- **Saving** (`stepsSave`): writes the day number and the counts to NVS every `STEP_SAVE_MS` (10 min), and always before power-off or the low-battery shutdown.
- **Restoring** (`stepsRestore`): runs once, as soon as the clock is known, which can be long after boot. It **adds** the saved day to whatever was counted in the meantime. `stepsSave` calls it first, so a late clock can't overwrite the record with a smaller count.

## 6. Coding decisions

| # | Decision | Why |
|---|---|---|
| S1 | Count in software from the FIFO, not with the chip's pedometer. | The chip's pedometer never counts on this board (§2). |
| S2 | 31.25 Hz and a 128-sample stream FIFO. | 4.1 s of buffer lets the sleeping watch wake only every 3 s. A higher rate means more wakes and more battery. The raise-to-wake motion engine works at this rate too ([raise to wake](2026-11-11-raise-to-wake-design.md), D4). |
| S3 | Detect peaks on \|a\| against a running average, with a 6-step entry run. | \|a\| doesn't depend on how the watch sits on the wrist. Requiring a run turns away single gestures. |
| S4 | Restart a jammed FIFO: after 3 empty reads in a row, or when data is waiting but nothing came out. Set up the motion engine again afterwards. | A read that fails halfway leaves the chip in FIFO read mode, and the FIFO never fills again. The restart's FIFO setup stops raise to wake, so `startMotion()` runs again (raise-to-wake spec D3). |
| S5 | Wi-Fi jobs run on a worker task on core 0 (audit fix `3f61f0a`). | A sync blocks for 10–40 s. When it ran on the loop, the FIFO overflowed and those steps were lost. Build 39 (before the merge) logged loop stalls of 5.1, 6.1 and 10.6 s around a setup page, a sync and a GitHub refresh. Each is longer than the FIFO's 4.1 s. |
| S6 | A corrected clock is not a midnight (`clockTakeJump`, audit fix). | NTP or a time-zone change could move the date and used to wipe the day's steps. |
| S7 | Restore once the clock is known and **add** to what was counted since boot; restore before every save (audit fix). | After a reboot the clock can come minutes later from NTP. The old code skipped the restore, then saved a smaller count over the record. |
| S8 | While asleep, a rejected raise sleeps out its cooldown on a **timer** and drains the FIFO on waking. | Raise to wake now fires on almost every arm swing while walking. Each rejection must still lead to a FIFO read, or a walk would go uncounted. |
| S9 | Save every 10 minutes, not on every step. | NVS wear. Cost: a crash or brownout loses up to 10 minutes of steps. |

## 7. Tuning values (`config.h`)

| Name | Value | Change it if… |
|---|---|---|
| `STEP_PEAK_G` | 0.06 g | too few steps: lower it. Arm waving or double counts: raise it. Wrist steps measure about 0.08–0.15 g. |
| `STEP_MIN_GAP_S` | 0.30 s | the fastest cadence (200 steps/min) |
| `STEP_MAX_GAP_S` | 2.0 s | a longer pause than this ends a walk |
| `STEP_ENTRY` | 6 | handling the watch adds steps: raise it. Short walks are missed: lower it. |
| `STEP_POLL_MS` | 3000 | must stay under the FIFO's 4.1 s |

## 8. Diagnostics: serial `s`

```
steps: 412 today, 380 reads, 11843 samples, 0 empty, 455 peaks, biggest swing 0.731 g, 0 FIFO restarts
steps: up 512 s (reset reason 1), wakes: 120 timer, 3 raise, 97 motion rejected, 2 other
```

- **samples ÷ uptime** should be about 31. Much less means samples were lost (FIFO overflow or long blocking).
- **peaks** but few **steps** means the entry run (`STEP_ENTRY`) is turning the walk away, or the peak threshold is borderline.
- **FIFO restarts** above 0 means jams. Each restart loses up to 4 s.
- **up** shorter than expected means the watch rebooted and lost unsaved steps. The **reset reason** says why: 1 power-on, 3 software restart, 4 crash, 6 task watchdog, 9 brownout, 11 USB/esptool.
- **motion rejected** climbing during a walk is normal (S8).

Run `s` straight after a test walk, before unplugging or flashing.

## 9. Test record (2026-11-11)

| Build | What happened |
|---|---|
| 39 (before the merge) | The loop stalled for 5.1, 6.1 and 10.6 s during the setup page, a sync and a GitHub refresh (`lag: loop` lines), each longer than the FIFO holds. Fixed by S5. |
| 41–42 | Shaking the watch counted (+9, +1, then +7, +3). The detector and FIFO work. |
| 43 | Walked unplugged: the count never rose. The watch was found freshly rebooted at about 03:04 (14 reads since boot), and the port had been gone for 2 minutes. No reset reason was captured: the boot line was lost and `s` didn't report it yet. |
| 44 | Walked unplugged: counted (confirmed by the user). Build 44 differs from 43 only in diagnostics (the uptime, reset reason and wake counters in `s`). The `s` numbers weren't read after this walk because the watch was still unplugged. |

**Root cause of the build 43 miss: not found.** No step-path code changed between 43 and 44. If it happens again, run `s` straight away: the uptime and reset reason will show whether a reboot was involved, and the samples and wake counters will show whether the FIFO was being drained.

## 10. Open questions and risks

- **Handling counts as steps.** Six or more arm movements in a row look like a walk (shaking the watch counted 10). Raise `STEP_ENTRY` or add a cadence-regularity check if it matters.
- **Vibration** (bus, bike) can pass as steps, for the same reason.
- **Accuracy hasn't been measured** against a known count over a long walk. The README's known issue stays until it has been.
- **Saving only every 10 minutes**: a crash or brownout loses up to 10 minutes of steps (S9).
- **The unexplained build 43 miss** (§9).
- **Serial resets.** The step counters once reset between two `s` queries with no flash in between. Closing a serial port from a PC may reset the chip (reset reason 11). Unconfirmed, but compare the uptime in `s` before trusting counts across a reconnect.
