# Wake-on-Weight

Optional feature: after a normal button/auto-off deep sleep, the scale wakes
itself on an RTC timer, briefly powers the load-cell rail, reads one raw
ADS1232 conversion, and either goes straight back to sleep (nothing placed)
or continues into a full normal boot (weight placed).

Runtime-gated: always compiled, off by default, no platformio environment or
custom-build flag. Requires the ADS1232 load-cell path (`ADS1232ADC`).

## Settings

Power menu row `Wake: Off / 0.5s / 1s / 2s`, cycling entry (same pattern as
`Drift:`), NVS key `wow_interval` under the `hds` namespace. Stored via
`storageEnsureInt(KEY_WOW_INTERVAL, 0)` in all three storage paths so legacy
EEPROM migration keeps `storageHasAllSettings()` satisfied. The stored
value defaults to off — the ECO behavior (~100 µA deep sleep, physical
button wake only).

Setting the interval does not arm anything by itself. Ticks only start from a
sleep that qualifies (see below).

## What wakes the device

Deep sleep still wakes on EXT1 (buttons + charging pin) exactly as before.
When Wake-on-Weight is active the RTC timer is a second wake source that
coexists with EXT1: a button press during a tick's sleep still EXT1-wakes
into a full boot.

## Arming (`wowCaptureBaselineForSleep` in `include/wake_on_weight.h`)

Called from `esp32_sleep()` in `include/power.h` just before
`scale.powerDown()`. Arms only when all of:

- `i_wow_interval > 0` (setting not Off)
- `i_lowBatteryCount == 0` (low battery disables the feature)
- `f_calibration_value != CALIBRATION_VALUE_DEFAULT` (scale is calibrated)
- `scale.getDebugInfo().validSamples > 0` (a live reading exists; setup-time
  sleeps and gyro accidental-touch boots never arm)

When armed it stores in `RTC_DATA_ATTR` (first use in this repo, survives
deep sleep without wearing NVS):

- `baselineRaw` = current `smoothedValue` (raw counts; the tare offset is
  common mode and cancels in the delta)
- `thresholdRaw` = `max(50 g × calibration factor, 500 counts)` floor
- `intervalUs` from the setting
- `magic` + `armed` markers

The timer is enabled by `wowArmSleepTimer()` in the same teardown chain,
**after** the energy-menu block's `esp_sleep_disable_wakeup_source`. When the
feature is off or disarmed the timer source is explicitly disabled again —
the RTC timer configuration otherwise persists across deep-sleep cycles and
would keep waking the device after the user turns the setting back to Off.

## Micro-wakeup (`wowMicroWakeOrContinue`)

First call in `setup()` (before reset-reason handling, NVS init, or any
peripheral init; the tick path never touches NVS or the battery). Only acts
when the wake cause is `ESP_SLEEP_WAKEUP_TIMER` and the `magic`/`armed`
markers are valid — every other boot falls straight through.

Per tick, in order:

1. Run at 80 MHz (`setCpuFrequencyMhz(80)`, the device's normal clock).
2. Release `gpio_hold` on `SCALE_SCLK`, `SCALE_PDWN`, `SCALE_DOUT`,
   `PWR_CTRL` only (per-pin first, then `gpio_deep_sleep_hold_dis()`);
   OLED/I2C/ACC/SCALE2 rails stay off and held.
3. `PWR_CTRL` HIGH, settle 10 ms, then a local `ADS1232_ADC` object (fresh
   mutexes after the full reset; `begin()` performs the PDWN reset pulse).
4. Poll `update()` for the first DRDY at 10 SPS with a 250 ms timeout
   (typical first conversion ≈ 100 ms; the "20 ms burst" from early feature
   drafts is not possible at 10 SPS — one sample costs ~100-130 ms).
5. `powerDown()` the ADC.

Decision:

- No sample within the timeout, or `dataOutOfRange` → back to sleep, retry
  next tick (no false boots on ADC faults).
- `|raw − baseline| <= threshold` → re-latch the four pins
  (`PWR_CTRL` LOW + hold), re-assert EXT1 wake pins + timer, `tickCount++`,
  `esp_deep_sleep_start()`.
- `|raw − baseline| > threshold` → clear `armed`, log, and **return into the
  normal `setup()` flow** (OLED, BLE, boot tare). `GPIO_power_on_with`
  stays -1 so BLE enables and the button-hold check is skipped, as for a
  timer wake today. The normal boot tare then zeros whatever was placed —
  same semantics as putting a cup on before pressing the button.

## Behavior notes

- Charging: arming is allowed (plugging in still EXT1-wakes into a full
  boot; a manual shutdown while charging may tick).
- Low battery: never arms once `i_lowBatteryCount > 0`; the micro path never
  reads the battery.
- Uncalibrated scale: setting can be changed but nothing arms until the
  scale is calibrated.
- Each tick prints one `[wow]` line on Serial before energy quieting exists.

## Power budget

10 SPS bounds the per-tick on-time (~110-130 ms at 80 MHz ≈ 8-15 mA plus the
~1-2 mA load-cell/ADC rail). Average current estimates for the three
intervals against the deep-sleep ~100 µA baseline (700 mAh cell):

| Setting | On-time / tick | Avg current | Standby |
| --- | --- | --- | --- |
| 0.5 s | ~130 ms | ~3.1 mA | ~9 days |
| 1 s | ~130 ms | ~1.7 mA | ~18 days |
| 2 s | ~130 ms | ~0.9 mA | ~33 days |

Measure with a real ammeter or the USB Sleep Test before trusting these;
they are estimates pending hardware confirmation.

## Related code

- `include/wake_on_weight.h` — micro-wake, baseline capture, timer arming
- `include/power.h` — `esp32_sleep()` arming hooks
- `include/menu.h` — Power menu cycling row
- `include/storage.h`, `include/parameter.h` — NVS key + globals
- `tools/test_wake_on_weight_contract.py` — read-only source contract
