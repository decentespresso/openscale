# SofronioDRS

SofronioDRS is a firmware-changing plugin for OpenScale. It adds DRS-style
extraction modes on top of the stock weighing screen and the stock
WebSocket/MDns stack.

## Features

- Pure weighing mode keeps the upstream behavior, including drift
  compensation, tracking, and stable output.
- Pour-over extraction mode with automatic timer start after a tare and
  automatic stop when the cup is removed.
- Espresso extraction mode with cup auto-tare, automatic timer start, and
  automatic stop after the last drop settles. Enter espresso extraction
  with an empty scale; a stable cup above 30 g is tared and starts the
  timer immediately.
- Extraction screen with live weight, timer, flow, dose, and brew ratio.
- `/apps/hds-drs/` web app with live values and controls.
- Standard BLE, USB, WiFi, OTA, and power behavior remain unchanged.

Drift compensation uses the upstream `include/scale_drift.h` state machine.
Extraction samples bypass the stable-output dead band and the moving-average
window: the drift-corrected weight is displayed directly, so both large and
small changes appear immediately. Residual ADC noise is shown as-is.

## Controls

- Holding both buttons at power-on opens the menu; the normal menu chord
  otherwise keeps the stock OpenScale USB-power rule.
- The `SofronioDRS` menu page has `DRS o/x` (master enable), separate
  pour-over dose rows (`Pour x.x +` / `Pour x.x -`), espresso dose rows
  (`Esp x.x +` / `Esp x.x -`), `Set Container`, and `Cont on/off` with the
  recorded container weight. Dose values persist per mode.
- With DRS enabled, the pure page uses the DRS layout (mode label, large
  weight, icons at the DRS positions), and extraction uses the DRS layout
  (weight, timer, first-drop seconds, flow, dose, ratio).
- `Set Container` closes the menu and shows a guided capture screen:
  tare with the circle button, then place the empty container. The first
  stable weight above 1 g is saved. Long-press the square button cancels
  the capture.
- Container subtraction applies to the DRS pure page only and mirrors the
  SofronioDRS range check: after enabling `Cont`, the container weight is
  subtracted only while the gross weight is within 5 g below to 25 g above
  the recorded container weight. Outside that window the gross weight is
  shown. The mode label shows `*` while subtraction is active. Extraction
  uses tare and is not affected.
- Short-press the square button in pure weighing to enter extraction and
  capture the displayed weight as the dose. In extraction the same press
  starts the timer, stops it, or resets it (start -> stop -> reset).
- Short-press the circle button to tare. In extraction it first stops
  the timer when running; when stopped it resets the timer and clears the
  extraction timestamps, then tares.
- Long-press the square button while extracting to return to pure
  weighing.
- Long-press the circle button to switch pour-over/espresso mode when DRS
  is enabled. An active app-text prompt still consumes the long press.
- The web app sends the text commands `drs toggle`, `drs pour`,
  `drs espresso`, `tare`, and `timer start|stop|zero` over `/snapshot`.

## WebSocket additions

`/snapshot` status frames add `drs_mode`, `drs_extraction`, `drs_flow`,
`drs_ratio`, and `drs_dose`. Weight frames add a `drs` object with
`extract`, `mode`, `flow`, `ratio`, `dose`, `timer`, and `trun`.

## Known limitations

- With DRS enabled, both the pure and extraction pages replace the stock
  weighing screen and do not show the app-text prompt overlay. Debug,
  about, and ADC recovery screens still take precedence. Disabling DRS
  restores the stock screen.
- Espresso auto-stop requires an observed first drop and at least seven
  seconds between first and last drop.
- Very slow pours can be mistaken for thermal drift; disable drift
  compensation from the Scale menu when the pour must be preserved.
- The web app requires WiFi. Extraction itself works on the scale without
  a connected client.
- DRS-specific BLE commands (0x30/0x31/0x32) are not implemented; the
  standard Decent BLE protocol is unchanged.

## Compatibility

- Requires `webserver`, `websocket`, and `littlefs`.
- Declared compatible with firmware refs `main` and `v3.1.17`.
- Conflicts with `pressensor` and `grind-by-weight`. The `grinder` feature
  is also declared incompatible because extraction weight would reach the
  grinder safety logic.

## Validation status

Validated on the plugin branch with PlatformIO Core 6.1.19:

- `esp32s3` normal firmware builds with the patch applied and the gate off.
- `esp32s3-hds-drs` builds with the gate on.
- `git apply --check --whitespace=error` passes for `main` and `v3.1.17`.
- The plugin environment verification applies the patch in an isolated
  checkout, runs `test_hds_drs_plugin_contract.py`, and builds.
- The production custom build completes.

Measured change between `esp32s3` and `esp32s3-hds-drs`:
10908 flash bytes and 240 static RAM bytes. The declared budget keeps
headroom at 12288 flash bytes, 1024 RAM bytes, and 9216 LittleFS bytes.

Physical scale, extraction, flow, OLED, button, and web-app behavior on
hardware has not been validated. A successful build is not hardware
evidence.

## License

GPLv3. Extraction behavior is derived from SofronioDRS; see the repository
license and the DRS project for original provenance.
