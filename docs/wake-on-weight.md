# Wake-on-Weight

Wake-on-Weight (WoW) defaults to off. When enabled, normal shutdown captures
the current weight, then RTC timer wakes briefly power the primary ADS1232
and discard the first conversion after ADC startup. The next two valid conversions
must both differ from the shutdown baseline in the same direction. A change greater than the
50 g threshold (with a 500-raw-count minimum) continues into normal boot,
including boot tare. Removing weight can
also trigger a boot. No separate firmware or energy-menu build is required.

## Setting and Timing

The Power menu row is `WakeOnWeight o / 2 / 3 / 4`. The confirmation says
`Off / Sleep 2s / Sleep 3s / Sleep 4s`. These are **sleep intervals**, not
detection periods. The NVS key `wow_interval` stores indices 0 through 3;
all default and migration paths use 0. The selected interval is snapshotted
at shutdown. Changing it does not change a sleep session already underway.

Enabling without a valid calibration displays `Please calibrate` and leaves the
setting off. If calibration becomes invalid while enabled, the next press turns it off.

The timer starts when entering deep sleep, after the micro-wake work.
The detection period is sleep interval plus micro-wake duration. The PR's
V8.1 measurement was approximately 850 ms per micro-wake, including about
300 ms ROM/Arduino startup, 100 ms rail settling, and the first 10 SPS ADC
conversion. Confirmation now adds two conversions, approximately 200 ms at 10 SPS.
Firmware polling has a shared 900 ms timeout for all three conversions after settling.
The revised path runs before Serial initialization and does not log per tick;
its exact duration must be remeasured on hardware.

| Sleep interval | Estimated detection period | Active duty |
| --- | --- | --- |
| 2 s | 3.05 s | 34.4% |
| 3 s | 4.05 s | 25.9% |
| 4 s | 5.05 s | 20.8% |

Duty is `1.05 / (sleep seconds + 1.05)`, not `1.05 / sleep seconds`.
ADC failures lengthen the active portion. Sampling is intermittent: a load
placed and removed between samples can be missed.

## Button and Charging Wake

Normal deep sleep keeps the existing EXT1 any-low button and charging wake
sources. A WoW micro-wake first releases their RTC holds and restores RTC input
mode and pull-ups before reading any wake pin. The pins are checked on entry,
every 2 ms during rail settling and ADC polling, after ADC cleanup,
and immediately before the final pin-latch/sleep sequence. A detected press
aborts the micro-wake and continues into normal boot without waiting for ADC
readiness. Square has priority over circle, and both have priority over
charging, matching the normal multi-pin wake mapping.

A button accepted during a micro-wake is remembered through setup, so release
during later initialization cannot send it back to sleep. Only this accepted
WoW button wake bypasses the subsequent button-hold gate. Ordinary EXT1 boots
retain their existing quick-boot/500 ms hold behavior. Circle/square BLE
selection and charging-only boot behavior remain unchanged. Weight-only boot
leaves `GPIO_power_on_with` at -1 as before.

Polling is not an edge latch: pulses shorter than the polling/scheduler gap,
or entirely within ROM/Arduino startup before setup, are not guaranteed.
The roughly 500 ms normal power-button press is longer than the measured
startup window. After the final check there are no intentional waits before
sleep; a normal press starting there remains low for EXT1. Test these
boundaries on hardware; host checks cannot establish electrical timing.

## Session Protection

Arming requires a valid interval, calibration, and an existing live sample.
An observed low-battery count or a known voltage below 3.2 V prevents arming.
Non-finite or overflowing calibration thresholds also prevent arming.

Wake-on-Weight has **no fixed session time limit**. Selecting a sleep interval
enables ongoing weight checks until a wake event or loss of battery power.
There are no recurring battery voltage measurements.
The low-voltage arming check does not enforce a 3.2 V cutoff during standby.
This user-selected mode can exhaust the battery; runtime depends on measured
whole-cycle consumption and battery capacity, not a guaranteed number of weeks.

ADC timeouts and unusable conversions are ignored, never treated as weight
changes. The next timer wake retries without a failure-count cutoff and without
recapturing the baseline. Each attempt retains its bounded read timeout and
returns to deep sleep between attempts. Button and charging wakes remain active.

No micro-wake initializes I2C, ADS1115, OLED, radio, storage, or battery ADC.
No NVS reads or writes occur. V8.1's `BATTERY_PIN` is only a placeholder;
using `analogRead(BATTERY_PIN)` would not measure its battery. Standby is not
a substitute for hardware cell protection, and can discharge below the normal
software cutoff.

## Resources and GPIOs

RTC state is an explicit 20-byte structure in `include/parameter.h`: magic,
armed flag, baseline, threshold, and sleep interval (including alignment padding).
The layout has a new magic.
Transient setup flags are not retained. No cross-task state is introduced.

The micro-wake releases only primary `SCLK`, `PDWN`, `DOUT`, and `PWR_CTRL`
holds. In builds without ESP-IDF power management, it runs at 20 MHz during
polling and restores the boot CPU frequency on continuation into setup.
PM-enabled builds, including the energy-menu build, leave frequency control
to ESP-IDF. Changing the hardware clock directly would bypass its frequency
and tick bookkeeping. Other per-pin holds stay in place.
Before re-sleep it holds the primary clock and PDWN low, DOUT as input,
and the main rail low, exactly as the normal sleep path does.

The temporary ADS1232 uses polling mode only. Every constructed instance
gets `powerDown()` followed by `end()` before either boot continuation or
deep sleep. The pinned driver's `end()` deletes its synchronization resources
without changing pin modes; no driver or dependency update is needed.

## Hardware Scope

Only **ESP32-S3 V8.1** has the PR's bench validation. Compilation remains
under `ADS1232ADC`; that macro alone is not a hardware validation claim.

- V8.0 and V7.5 share the primary ADC pins 11/12/13, rail pin 3, and RTC
  wake pins 1/2/10 with V8.1. Their pin maps support the same micro path,
  but their electrical settling/current behavior has not been validated.
- V7.4 uses primary pins 8/9/18 and wake pins 1/2/6, also compatible with
  S3 digital holds and RTC inputs. Its secondary GPIO35 can conflict with
  some PSRAM modules, an existing board/sleep constraint.
- V7.3 and V7.2 have legacy missing secondary-scale/accessory macros in the
  surrounding normal sleep implementation. V6/V5 lack the required power
  and charging definitions. These legacy configurations cannot be certified
  by enabling `ADS1232ADC`; no unrelated board repair is included here.

GPIO3 is a strapping pin. Existing board pull/load assumptions are unchanged.
Primary ADC pins need digital hold support, not RTC wake capability; only
the buttons and charging inputs need RTC capability. Older compatible
revisions are not unnecessarily disabled, but require board-level testing.

## Power Estimate

With the illustrative assumptions of 6 mA average **across the entire active
window** and 0.1 mA asleep, `Iavg = duty * 6 + (1 - duty) * 0.1`:

| Sleep interval | Estimated average while WoW cycles |
| --- | --- |
| 2 s | 2.13 mA |
| 3 s | 1.63 mA |
| 4 s | 1.33 mA |

The startup part runs at the boot clock, not 20 MHz. PM-enabled builds also
use their configured frequency policy during polling. The 6 mA assumption is
not a measured whole-cycle current and may underestimate consumption.
Standby duration needs hardware measurement; there is no fixed session cap.
An unavailable ADC causes ongoing retries and can increase standby consumption.
Measure complete cycles with an ammeter, including startup and failed reads.

## Verification

Run `python tools/test_wake_on_weight_contract.py` and
`python tools/test_wake_on_weight_runtime.py`, then build `esp32s3` and
`esp32s3-energy-menu`. Hardware follow-up should sweep normal button presses
across startup, rail settling, ADC wait, and sleep handoff; check both buttons,
charging insertion, ADC disconnect/recovery, extended standby, boot tare,
resource cleanup, and whole-cycle current. WoW-off sleep must remain unchanged.
