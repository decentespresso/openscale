# PR: HDS 9.0.5 support — BQ27427 battery fuel gauge

## Summary

HDS 9.0.5 hardware adds a **TI BQ27427YZFR single-cell Li-ion fuel gauge**
(fixed I2C address 0x55, Impedance Track algorithm) compared to 8.x boards.
This PR adds detection, initialization, battery info, and battery
protection while keeping full compatibility with 8.x hardware —
**one single firmware binary serves all boards**, the board type is
detected at runtime.

## Design principles

- **Single firmware, runtime detection**: one PlatformIO env (`esp32s3`).
  At startup the firmware probes I2C address 0x55 (with retries for
  cold-start timing) and verifies DEVICE_TYPE == 0x0427. When the gauge is
  present (9.0.5), battery voltage/percent come from the gauge, GPIO6
  becomes CHRG_CTRL (charge enable) and GPIO14 is the gauge GPOUT. When
  absent (8.x), the legacy ADC/ADS1115 battery path is used and GPIO6 stays
  the battery ADC input. No compile-time board variant is needed.
- **Clean config structure**: the `V8_1` pin block stays pure 8.1; the
  9.0.5 differences (CHRG_CTRL=6, GPOUT=14) live in a separate section
  with a comment explaining the runtime role switch.
- **ADS1115 support retained**: `ADS_init()` is gated on `!b_hasFuelGauge`;
  boards that later drop the ADS1115 fall back to the existing internal-ADC
  path.
- **No changes to existing menu framework logic**: new menu items are
  registered with the existing mechanism. `mainMenuSize()` centralizes the
  effective main-menu size so returning from a submenu cannot resurrect
  hidden items (review fix).

## Changes

| File | Change |
|---|---|
| `include/config.h` | 9.0.5 differences section (CHRG_CTRL=6, GPOUT=14) after the pure 8.1 block |
| `include/fuel_gauge.h` | Driver API (pin macros overridable) |
| `src/fuel_gauge.cpp` | Detection with retry, Chem 1202 enforcement, CC_GAIN sign-bit fix, read API, charging via TP4056 CHRG pin, low-SOC notify, design-capacity query/set, deep-sleep hook |
| `include/fuel_gauge_menu.h` | Bat. Info menu: full dual-column page with gauge, 4-line page without |
| `include/menu.h` | Register `menuBatInfo` + `menuBatteryProtect`; `mainMenuSize()`; `compactMainMenu()` hides Battery Protect on gauge-less boards; runtime battery paths |
| `include/parameter.h` | New globals `b_hasFuelGauge`, `b_batteryProtect` |
| `include/power.h` | `batteryPercent()` (gauge SOC or legacy map, clamped 0-100); runtime battery paths |
| `include/storage.h` | `KEY_BAT_PROTECT`, `KEY_BAT_CAPACITY_SET` (+ migration path) |
| `include/usbcomm.h` | `bc` command (query/set design capacity); runtime-gated legacy commands |
| `include/decent_protocol.h`, `include/websocket.h` | Battery percent via `batteryPercent()` |
| `src/hds.ino` | `fuelGaugeBegin()` + `compactMainMenu()`; one-time design-capacity write (700 mAh); runtime battery paths |
| `platformio.ini` | Gauge library dep; CPU 80 MHz config; single `esp32s3` env |
| `tools/bq27427_probe/` | I2C diagnostics probe project |
| `.gitignore`, `lib/README.md` | Build artifacts, library source notes |

## Library dependency

`edrean/BQ27427 Battery Fuel Gauge Arduino Library @ 1.0.4` (MIT, resolved
automatically from the PlatformIO registry; upstream:
https://github.com/edreanernst/BQ27427_Arduino_Library)

## Features

### 1. Boot detection and initialization

1. Probe I2C address 0x55 with retries; no ACK -> treat as 8.x.
2. Read CONTROL + DEVICE_TYPE (0x0001), verify 0x0427.
3. Chemistry check: if CHEM_ID != 1202 run the TRM switch flow with long
   CFGUPMODE timeouts and sealed-state restore (the library's setChemID
   uses a 50 ms timeout and its chemID()/CHEM_B comparison never matches
   the hex-nibble encoding the chip returns). Hard-coded for the 4.2 V cell.
4. CC_GAIN sign-bit fix: early batches ship a negative coulomb-counter gain
   which inverts current/power readings; the value lives in RAM and resets
   on POR, so it is re-checked and fixed on every boot.
5. Design capacity: written **once** (700 mAh) when the NVS flag
   `KEY_BAT_CAPACITY_SET` is unset, so reflashing firmware never resets the
   value. The `bc` USB command can query/set it manually (300-2000 mAh
   range, user-initiated, does not touch the flag).

### 2. Bat. Info menu

- With gauge (9.0.5): one dual-column page — voltage, chip temp, current,
  power, capacity, FCC, health, battery level, charging, USB; plus a note
  page. 500 ms live refresh; ENTER cycles, NEXT steps back (exit on first
  page).
- Without gauge (8.x): 4-line page — voltage, battery level, charging, USB.
  Battery percent is clamped to 0-100 (the legacy voltage map extrapolates
  above 100 % at full charge).
- Charging state reads the **TP4056 CHRG pin** (GPIO10, low = charging).

### 3. Battery Protect (9.0.5, off by default, persisted in NVS)

Menu on/off. When enabled, SOC >= 80 % pulls CHRG_CTRL low to cut off the
charger; charging resumes at 75 % (hysteresis). Verified on hardware
(cutoff triggered at 97 % on a nearly-full battery).

### 4. Low-SOC notify

Serial notification (once per transition) when SOC drops below 10 %.

## Testing

All boards run the same firmware binary.

### HDS 9.0.5 (with BQ27427)

| Item | Result |
|---|---|
| Device identification | DEVICE_TYPE 0x0427, FW 0x0202 |
| Chemistry | 1202 (4.2 V) enforced, persists across reboots |
| CC_GAIN | negative sign bit detected and fixed at boot (0xB4 -> 0x34) |
| Design capacity | set once to 700 mAh; FCC then re-evaluated (1256 -> 646 mAh, SOH ~92 %) |
| Charging (CHRG pin) | low = charging, verified |
| Bat. Info menu | dual-column page, cycling, 500 ms refresh |
| Battery Protect | CHRG_CTRL gating verified (cutoff at 97 %) |
| `bc` command | query + write verified |
| Weighing | normal operation, no regression |

### HDS 8.3.1 (no BQ27427)

| Item | Result |
|---|---|
| Boot | normal |
| Gauge probe | skipped cleanly after retries |
| Bat. Info menu | 4-line page (voltage/level/CRG/USB), percent clamped |
| Weighing | normal operation, no regression |

### Notes

- 8.2.0 board: new-NVS migration path was missing the new keys, which
  blocked first boot; fixed (keys are now ensured in the migration path
  too). The board's weighing readouts were erratic without a load cell
  attached — hardware setup, not firmware.
- ADS1232 effective-resolution test screen lives in a separate copy
  (`openscale9_0_5_bittest/`, not part of this PR): 3 rounds of 10 s /
  100 samples each, reporting min/max/range/distinct/effective bits and
  the minimum division for a 2 kg range. 8.3.1 measured ~17.2-17.6
  effective bits (~0.01 g division); 9.0.5 similar.

## Out of scope / follow-ups

- [ ] Deep-sleep hook wiring into the actual `power.h` sleep path (API only
      in this PR); GPOUT (GPIO14) available for SOC_INT wake-up
- [ ] BLE command for design capacity (USB `bc` only for now)
- [ ] Menu registration single-point refactor (separate task)
- [ ] Long-term observation of FCC/SOH learning convergence (~1-2 full cycles)
- [ ] Cycle-count feature: not supported by BQ27427 (no CycleCount command);
      host-side counting or SOH can be used instead

## References

- TI datasheet SLUSEBSA / TRM SLUUCD5 (see `docs/9.0.5/`, Chinese translation included)
- Key protocol details (Control() read method, chem-switch flow, SOH at 0x20,
  CC_GAIN sign-bit issue, STOP-terminated writes) documented in
  `docs/9.0.5/BQ27427_中文手册.md` and `BQ27427_Notes_EN.md`
