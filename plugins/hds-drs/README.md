# SofronioDRS

SofronioDRS adds pour-over and espresso extraction screens to OpenScale and
a WiFi dashboard at `/apps/hds-drs/`. You can use extraction on the scale with
WiFi off.

## Enable DRS

DRS starts off on a fresh install. In Settings, open **SofronioDRS** and
select the **DRS** row to turn it on. Select the same row again to turn it off.

DRS, Pressensor, and Grind by Weight can share a custom build. Settings save
one active integration across restarts; choosing one turns the others off.
If the grinder has not confirmed a safe stop, Settings block the change and
show **Confirm Stop**. Confirm the grinder has stopped before retrying.

## Controls

- **Square, short:** Enter extraction from weighing. DRS uses the displayed
  dose if it is at least 3 g, or the configured dose for the current mode.
  In extraction, short press cycles through start, stop, and reset.
- **Square, long:** Leave extraction or cancel container capture.
- **Circle, short:** Tare. In extraction, this stops a running timer or
  resets a stopped one before taring.
- **Circle, long:** Switch between pour-over and espresso. App Text takes
  display priority while a prompt is visible; long press dismisses it.

For pour-over, tare before pouring. The timer starts on a weight gain above
1 g after the three-second delay and stops when you remove the cup (a drop of
at least 50 g from a stable reading). For espresso, enter extraction with an
empty scale. Placing a stable cup over 30 g starts the timer once the cup tare
completes. Auto-stop requires a first drop and settled weight at least seven
seconds after that drop. The OLED shows weight, timer, first-drop time (in
espresso), flow, dose, and brew ratio. If drift compensation masks a slow
pour, adjust it in the Scale menu.

Use **Set Container** to capture the empty container. Tare the clear scale,
place the container, and wait for a stable reading of at least 1 g. Toggle
**Cont** to subtract the container during weighing when gross weight is
between container weight minus 5 g and plus 25 g. The mode label shows `*`
while subtraction applies. Extraction weight uses tare instead.

## WebSocket additions

The dashboard sends `drs toggle`, `drs pour`, `drs espresso`, `drs tare`, and
`drs timer start|stop|zero` to `/snapshot`. Its controls require an active DRS
connection; the timer controls also require extraction. Dashboard mode
changes apply for the current boot without writing settings.

Status frames add `drs_enabled`, `drs_mode`, `drs_extraction`, `drs_weight`,
`drs_flow`, `drs_ratio`, and `drs_dose`. Weight frames add `drs` with
`enabled`, `extract`, `mode`, `weight`, `flow`, `ratio`, `dose`, `timer`, and
`trun`. DRS presentation weight lives in `drs_weight` or `drs.weight`; the
top-level `grams` field retains stock weight. The mode is `0` for pour-over
and `1` for espresso; timer values are in seconds.

## Compatibility

- Requires `webserver`, `websocket`, and `littlefs`.
- Supports firmware refs `main` and `v3.1.17`. It can share a build with
  `pressensor`, `grind-by-weight`, `energy-menu`, and `default-web-apps`.
- Uses existing WiFi setup and standard Decent BLE and USB interfaces.
  DRS-specific BLE commands are not implemented.
- Stores container settings, dose defaults, and locally selected brew mode
  in the `hds-drs` NVS namespace. Missing settings use pour-over mode, doses
  of 16 g and 18 g, and no container subtraction. DRS adds no required keys
  to the base settings store.
- Earlier PR builds' shared `hds` entries are left untouched, not imported.
  Configure DRS once after updating from those builds.

## Validation status

Run `node plugins/hds-drs/test_web.cjs` for dashboard status and control checks
with mocked WebSocket frames. The firmware patch includes host checks for
extraction, storage, integration handoff, and OLED priority, plus the plugin
contract used by the custom-build pipeline. This revision has not undergone
live hardware validation.

## License

GPLv3. Extraction behavior is derived from SofronioDRS; see the repository
license and the DRS project for original provenance.
