# PR: HDS 9.0.5 support — BQ27427 battery fuel gauge

## Summary

HDS 9.0.5 hardware adds a **TI BQ27427YZFR single-cell Li-ion fuel gauge**
(fixed I2C address 0x55, Impedance Track algorithm) compared to 8.3.1.
This PR adds detection, initialization, and a battery info menu for the new
chip while keeping full compatibility with 8.3.1 and earlier hardware.

## Design principles

- **Probe at boot, enable on demand**: at startup the firmware probes I2C
  address 0x55 and verifies DEVICE_TYPE == 0x0427. If the gauge is absent,
  all battery features (menu item, deep-sleep hook, chemistry setup) stay
  disabled and the 8.3.1 behavior is unchanged.
- **ADS1115 support retained**: when the gauge is present, the ADS1115 is
  skipped (voltage comes from the gauge); otherwise the legacy ADS1115 path
  is used. Boards that later drop the ADS1115 fall back to the existing
  internal-ADC path.
- **No changes to existing menu framework logic**: the new menu item is
  registered with the existing mechanism; menu navigation/rendering code is
  untouched. On boards without the gauge the item is removed from the menu
  array at boot (`compactMainMenu`), so 8.3.1 menus look exactly as before.

## Changes

| File | Change |
|---|---|
| `include/fuel_gauge.h` | New: driver API (pin macros overridable; defaults SDA=5 / SCL=4 / USB_DET=8) |
| `src/fuel_gauge.cpp` | New: detection, Chem ID 1202 enforcement, read API, charging logic, deep-sleep hook |
| `include/fuel_gauge_menu.h` | New: Bat. Info paged menu (header-only, follows the showStatus pattern) |
| `include/menu.h` | Register `menuBatInfo` at the end of the main menu; add `compactMainMenu()` |
| `include/parameter.h` | New global `volatile bool b_hasFuelGauge` |
| `src/hds.ino` | Call `fuelGaugeBegin()` + `compactMainMenu()` after `Wire.begin` |
| `platformio.ini` | Add gauge library to `lib_deps`; CPU 80 MHz config |
| `.gitignore` | Ignore build artifacts (sdkconfig, managed_components, etc.) |
| `lib/README.md` | Library source notes (registry auto-download, no vendoring) |

## Library dependency

`edrean/BQ27427 Battery Fuel Gauge Arduino Library @ 1.0.4` (MIT, resolved
automatically from the PlatformIO registry; upstream:
https://github.com/edreanernst/BQ27427_Arduino_Library)

## Features

### 1. Boot detection and initialization

1. Probe I2C address 0x55; no ACK -> treat as 8.3.1, disable all gauge features.
2. Read CONTROL + DEVICE_TYPE (0x0001), verify 0x0427.
3. Chemistry check: if CHEM_ID != 0x1202 run the TRM switch flow
   (UNSEAL -> SET_CFGUPDATE -> CHEM_B -> SOFT_RESET), persisted in NVM.
   Hard-coded for the 4.2 V cell of this product (default 4.35 V profile
   does not match).

### 2. Bat. Info menu (9.0.5 boards only, last menu item)

3 data pages, ENTER cycles forward / NEXT steps back (exit on first page),
500 ms live refresh:

- Page 1: Voltage (2 decimals), Chip temp, Current, Power, Capacity
- Page 2: FullCap, Health, Bat. Level, Charging, USB
- Page 3: note that true capacity/health show after full charge/discharge cycles

Charging detection (triple check): `USB plugged && gauge not discharging
(DSG=0) && current > 30 mA`.

### 3. Deep-sleep hook

`fuelGaugeSleep()` is provided for the deep-sleep path (currently a no-op:
the gauge self-enters SLEEP at 9 uA under low load; SHUTDOWN can be added
for longer sleeps). Skipped automatically when the gauge is absent.

## Testing

Both boards tested on hardware, 2026-08-14.

### HDS 9.0.5 (with BQ27427)

| Item | Result |
|---|---|
| Device identification | DEVICE_TYPE 0x0427, FW 0x0202 |
| Chemistry | switched to 0x1202 (4.2 V), persists across reboots |
| Battery readings | 4162 mV / 99 % / 1237 mAh / SOH 94 % / 27.1 C |
| Charging / discharging | USB unplugged -> Charging No; USB plugged + charging -> Yes |
| Bat. Info menu | 3 pages, page cycling, exit, 500 ms refresh |
| Weighing | normal operation, no regression |

### HDS 8.3.1 (no BQ27427)

| Item | Result |
|---|---|
| Boot | normal (Begin! / Setup complete) |
| Gauge probe | skipped cleanly, no error |
| Main menu | no Bat. Info item (removed at boot) |
| Weighing | normal operation, no regression |

## Out of scope / follow-ups

- [ ] Wire the deep-sleep hook into the actual `power.h` sleep path (API only in this PR)
- [ ] Menu registration single-point refactor (separate task)
- [ ] Long-term observation of FCC/SOH learning convergence (~1-2 full cycles)
- [ ] Cycle-count feature: not supported by BQ27427 (no CycleCount command);
      host-side counting or SOH can be used instead

## References

- TI datasheet SLUSEBSA / TRM SLUUCD5 (see `docs/9.0.5/`, Chinese translation included)
- Key protocol details (Control() read method, chem-switch flow, SOH at 0x20,
  STOP-terminated writes) documented in `docs/9.0.5/BQ27427_中文手册.md`
  and `BQ27427_Notes_EN.md`
