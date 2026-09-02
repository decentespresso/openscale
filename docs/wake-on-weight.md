# Wake-on-Weight

Optional feature: after a normal button/auto-off deep sleep, the scale wakes
itself on an RTC timer, briefly powers the load-cell rail, reads one raw
ADS1232 conversion, and either goes straight back to sleep (nothing placed)
or continues into a full normal boot (weight placed).

Runtime-gated: always compiled, off by default, no platformio environment or
custom-build flag. Requires the ADS1232 load-cell path (`ADS1232ADC`).

## Setting

Power menu row `Wake: Off / 2s / 3s / 4s` (cycling entry, same pattern as
`Drift:`), NVS key `wow_interval` (int) under the `hds` namespace. Stored via
`storageEnsureInt(KEY_WOW_INTERVAL, 0)` in all three storage paths so legacy
EEPROM migration keeps `storageHasAllSettings()` satisfied. The stored value
defaults to off (index 0) — the ECO behavior (~100 µA deep sleep, physical
button wake only).

Changing the menu does not re-arm a sleep already in progress: the interval
is snapshotted into RTC memory at each sleep entry, so a running tick cycle
keeps its old cadence until the next qualifying sleep.

## Tick cadence

Measured on V8.1, one tick costs ~850 ms end to end: ≈300 ms ROM/Arduino
boot at 240 MHz before the tick code runs, ~100 ms rail settle, ~400 ms
until the first ADS1232 conversion at 10 SPS, then the re-latch and
re-sleep. Duty cycles by setting:

| Setting | Duty | Notes |
| --- | --- | --- |
| Off | — | ECO mode, no ticks |
| 2 s | ~43% | barely leaves time asleep; battery-heavy |
| 3 s | ~28% | usable compromise |
| 4 s | ~21% | recommended default |

The tick downclocks to the lowest supported CPU frequency, 20 MHz
(`setCpuFrequencyMhz(20)`, S3 minimum below the 240/160/80 PLL tiers), for
the ADC wait and restores the frequency the chip booted at (`bootFreqMhz`)
if weight is detected, so a detected full boot behaves like any other full
boot.

## What wakes the device

Deep sleep still wakes on EXT1 (buttons + charging pin) exactly as before.
The RTC timer is a second wake source that coexists with EXT1: a button press
during a tick's sleep still EXT1-wakes into a full boot.

## Arming (`wowCaptureBaselineForSleep` in `include/wake_on_weight.h`)

Called from `esp32_sleep()` in `include/power.h` just before
`scale.powerDown()`. Arms only when all of:

- `i_wow_interval > 0` (setting not Off)
- `i_lowBatteryCount == 0` (low battery disables the feature)
- `f_calibration_value != CALIBRATION_VALUE_DEFAULT` (scale is calibrated;
  signed calibration factors are supported via `fabsf`)
- `scale.getDebugInfo().validSamples > 0` (a live reading exists; setup-time
  sleeps and gyro accidental-touch boots never arm)

When armed it stores in `RTC_DATA_ATTR` (first use in this repo, survives
deep sleep without wearing NVS):

- `baselineRaw` = current `smoothedValue` (raw counts; the tare offset is
  common mode and cancels in the delta)
- `thresholdRaw` = `max(50 g × |calibration factor|, 500 counts)` floor
- `intervalUs` from the menu setting (snapshot)
- `magic` + `armed` markers

The timer is enabled by `wowArmSleepTimer()` after the baseline capture —
**after** the energy-menu block's `esp_sleep_disable_wakeup_source` — and
only when armed; the disable is gated on "enabled earlier in this boot" so
ordinary sleeps never trip the ESP-IDF "Incorrect wakeup source" error.

## Micro-wakeup (`wowMicroWakeOrContinue`)

First call in `setup()` (before reset-reason handling, NVS init, or any
peripheral init; the tick path never touches NVS or the battery). Only acts
when the wake cause is `ESP_SLEEP_WAKEUP_TIMER` and the `magic`/`armed`
markers are valid — every other boot falls straight through.

Per tick, in order:

1. Log tick count + boot frequency, then run at 20 MHz.
2. Release `gpio_hold` on `SCALE_SCLK`, `SCALE_PDWN`, `SCALE_DOUT`,
   `PWR_CTRL` only (per-pin first, then `gpio_deep_sleep_hold_dis()`);
   OLED/I2C/secondary-scale/`ACC_PWR_CTRL` rails stay off and held.
3. `PWR_CTRL` HIGH, settle 100 ms, then a local `ADS1232_ADC` object (fresh
   after the full reset; `begin()` performs the PDWN reset pulse).
4. Poll for the first DRDY at 10 SPS with a 900 ms timeout (measured on
   hardware: first conversion arrives ~400 ms after rail power; the original
   250 ms window never caught it).
5. `powerDown()` the ADC.

Decision:

- No sample within the timeout, or `dataOutOfRange` → back to sleep, retry
  next tick (no false boots on ADC faults).
- `|raw − baseline| <= threshold` → re-latch the four pins
  (`PWR_CTRL` LOW + hold), re-assert EXT1 wake pins + the snapshotted
  interval timer, `tickCount++`, `esp_deep_sleep_start()`.
- `|raw − baseline| > threshold` → clear `armed`, restore the boot frequency,
  log, and **return into the normal `setup()` flow** (OLED, BLE, boot tare).
  `GPIO_power_on_with` stays -1 so BLE enables and the button-hold check is
  skipped, as for a timer wake today. The normal boot tare then zeros
  whatever was placed — same semantics as putting a cup on before pressing
  the button.

Measured idle behavior on the bench (empty scale, 4 s ticks): consecutive
tick samples drift ~70-90 raw counts (~7-9 g equivalent at a ~1000 counts/g
calibration) — thermal drift, comfortably under the 50 g threshold.

## Behavior notes

- Charging: arming is allowed (plugging in still EXT1-wakes into a full
  boot; a manual shutdown while charging may tick).
- Low battery: never arms once `i_lowBatteryCount > 0`; the micro path never
  reads the battery.
- Uncalibrated scale: setting can be changed but nothing arms until the
  scale is calibrated.
- Each tick prints a short `[wow]` line on Serial before energy quieting
  exists.

## Power budget

Per tick ≈ 850 ms active at 20 MHz (below the PLL tiers the draw is
dominated by the load-cell/ADC rail; measure on hardware) against the
deep-sleep ~100 µA baseline (700 mAh cell). Rough estimates assuming
~6 mA active:

| Setting | Duty | Avg current | Standby |
| --- | --- | --- | --- |
| 2 s | ~43% | ~2.6 mA | ~11 days |
| 3 s | ~28% | ~1.8 mA | ~16 days |
| 4 s | ~21% | ~1.3 mA | ~22 days |

Measure with a real ammeter or the USB Sleep Test before trusting these;
they are estimates pending hardware confirmation.

## Related code

- `include/wake_on_weight.h` — micro-wake, baseline capture, timer arming
- `include/power.h` — `esp32_sleep()` arming hooks
- `include/menu.h` — Power menu cycling row
- `include/storage.h`, `include/parameter.h` — NVS key + globals
- `tools/test_wake_on_weight_contract.py` — read-only source contract
