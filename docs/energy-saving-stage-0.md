# Energy Saving Framework

The framework is compiled only when `HDS_ENABLE_ENERGY_MENU=1`. Standard firmware remains unchanged; the dedicated PlatformIO environment is `esp32s3-energy-menu`.

## Features

The `Energy Saving` menu places `Back` first and exposes six independent features:

1. Serial Quiet
2. Power Cadence
3. OLED Redraw
4. OLED Idle
5. OLED Static
6. Motion Poll

On V8.1 builds with `ACC_PWR_CTRL` and no accelerometer, `ACC Rail Off` is available after the six features. There is no master switch or energy status page. All features default off after an energy schema migration.

## Runtime Dispatch

Each feature runs at its existing execution point:

- Serial Quiet is checked only before recurring informational serial output.
- Power Cadence is checked only by battery, charging, and auto-off scheduling.
- OLED Redraw and OLED Static are checked only at normal OLED render opportunities.
- OLED Idle uses 100 ms cadence housekeeping from the OLED path.
- Motion Poll is checked only when motion data is requested.

Shared OLED Idle housekeeping reads the in-memory feature mask and returns immediately when the mask is empty or OLED Idle is not enabled. No preferences are read from the main loop.

Feature changes are applied immediately. Disabling a feature resets only its own cache or cadence state. Disabling OLED Idle restores Active display behavior unless a protocol command explicitly switched the display off.

## Persistence

The existing `hds` Preferences namespace stores one Boolean for each available feature. Energy schema version 4 initializes current features to off when the schema is old, missing, or unknown.

## Baseline Behavior

With all six features off, normal OLED rendering, motion reads, serial output, battery checks, auto-off behavior, and BLE, USB, and WebSocket transport cadences remain unchanged. Explicit soft-sleep and display protocol commands remain available independently of the energy feature framework.
