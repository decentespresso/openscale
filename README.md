# Decent Open Scale (aka "openscale")
Decent Open Scale
Copyright 2024 Decent Espresso International Ltd

Credits:
Invention and authorship: Chen Zhichao (aka "Sofronio")

# Introduction:
The Decent Open Scale is a full open sourced(software/hardware/cad design) BLE scale. Currently you can use it with de1app and Decent Espresso machine. But with Decent Scale API, you can use it for anything.<br />
To make it work, you need at least an ESP32 for MCU, a Loadcell for weighing, an HX711 for Loadcell ADC, a SSD1306 for OLED display, a MPU6050 for gyro function.<br />
The current PCB for HDS uses ESP32S3N16, i2000 2kg loadcell, ADS1232(instead of HX711), SH1106/SH1116(instead of SSD1306), ADS1115(for battery voltage), TP4056(battery charging), CH340K(COM to USB), REF5025(analog VDD), BMA400(instead of MPU6050, but gyro will be dropped in the future).
If you want to use it unplugged, you'll also need a 3.7v battery.<br />
If you only want to burn the firmware, please read How to upload HEX file.<br />

# How to build

Beginning with firmware version 3.0.0, OpenScale now uses [Platform.io](https://platformio.org) for development, building and flashing the firmware.  

To build and flash the scale from your computer, make sure you have the `pio` command line tool installed.  
After this, one can simply use `$pio run -t upload` and platformio will build and flash the firmware to the connected Esp32s3 chip.


# New Features in 3.0.0

## Wifi mode

Decent Scale uses WiFi for web apps, WebSocket data streaming, and OTA updates.

To enable WiFi mode, go to HDS setup menu and find "Wifi settings" entry. From there you can enable/disable WiFi as well as see current WiFi details. If you toggle WiFi on/off, a restart of the scale is required for the new settings to take effect.

**First-time setup:** if no WiFi credentials are stored, the scale opens its own access point — SSID `Decent Scale`, password `12345678`, IP `192.168.1.1`. Connect to it and navigate to [hds.local](http://hds.local) to enter your home WiFi credentials. The scale will restart and connect to your network.

**Reconnect:** once configured, the scale stays in STA mode and reconnects automatically after signal loss with exponential backoff (5 s → 10 → 20 → 40 → 60 s cap). It no longer falls back to the AP — a configured scale keeps retrying your home network.

**Discovery:** the scale advertises itself via mDNS (`hds.local`) and DNS-SD (`_decentscale._tcp`) so apps can discover it on the LAN without knowing the IP.

## Tasmota grinder mode

HDS can optionally control a selected Tasmota grinder plug over local TCP. This feature is intended for trusted local WLANs only.

Full setup, usage, FAQ, and troubleshooting are documented in [docs/hds-tasmota-grinder.md](docs/hds-tasmota-grinder.md).

## Web apps
The same web apps that could be used with Half Decent Scale from [the web](https://decentespresso.com/docs/introducing_half_decent_scale_web_apps) have now been rewritten to run directly from the Half Decent Scale.

You can find these apps on the index page of Half Decent Scale Web User Interface - navigating to [hds.local](http://hds.local)

## WebSockets support

When WiFi mode is enabled on HDS, there is also a new WebSocket endpoint available, where you can receive real-time information regarding the weight in the following format  

```json
{
  "grams": 25.66,
  "ms": 12345
}
```

you can also send a simple `tare` String over the websocket and the scale will tare itself.

For backwards compatibility, snapshot frames do not include a `type` field.
Clients should treat the absence of `type` as a weight snapshot. Event and
response frames always include `type`.

> **Security / trust model:** the WebSocket endpoint is unauthenticated, just
> like the BLE interface. Any host that can reach the scale on the network can
> read weight and send control commands, including destructive ones such as
> `power off`, `soft_sleep on`, and `display off` (a remote `power off` cannot
> be woken over the network). Only expose HDS on a trusted LAN.

Multiple clients may connect at once (e.g. the on-device web UI and a separate
app). They share a single negotiated stream rate (the most recent `rate` command
applies to all clients); each client is served independently, so a backed-up
client drops its own frames without stalling the others.

By default, WiFi clients receive weight snapshots at 2 Hz. A connected client
can negotiate one of the supported WiFi stream rates by sending one of these
WebSocket messages:

```text
rate 2k
rate 5k
rate 10k
```

or JSON (any of these forms work):

```json
{ "command": "rate", "value": "10k" }
{ "rate": "10k" }
{ "rate_hz": 10 }
{ "hz": 10 }
{ "interval_ms": 100 }
```

The supported rates are 2 Hz, 5 Hz, and 10 Hz. The firmware sends back a
`type: "rate"` acknowledgement with the active interval and rate. The firmware
does not automatically downgrade a requested rate on weak WiFi; if a client's
socket is not writable for a tick, that frame is skipped for that client rather
than queued.

WiFi snapshots are intentionally kept small, especially at 10 Hz:

```json
{
  "grams": 25.66,
  "ms": 12345
}
```

Battery, charging, timer, and power/display state are sent in typed `status`
frames on demand only. Send `status`, `battery`, or `info` to get one. The
firmware does not push status periodically (mirrors the BT side, which has no
periodic state push either — clients request battery via `0x22`, etc.).

WiFi clients can send these text commands over the same WebSocket:

```text
tare
events on
events off
timer start
timer stop
timer reset
display on
display off
low_power on
low_power off
soft_sleep on
soft_sleep off
power off
status
battery
info
```

The legacy text `tare` command is intentionally silent for backwards
compatibility. JSON `{ "command": "tare" }` returns a `type: "status"` ack.

The same commands can be sent as JSON (full form):

```json
{ "command": "tare" }
{ "command": "timer", "action": "start" }
```

**JSON shorthand** — control keys at the top level are also accepted:

```json
{ "events": "on" }
{ "display": "off" }
{ "tare": true }
{ "timer": "start" }
```

Bool values map to on/off (`{"display":false}` → off). The `power` key is excluded from the bool form — power-off requires the explicit `{"command":"power","action":"off"}` or text `power off`.

An unrecognized or malformed command (bad JSON, or an unknown verb) returns an
`error` frame rather than silence, so a client can distinguish a rejected
command from a frame that never arrived:

```json
{
  "type": "error",
  "code": "unknown_command",
  "message": "unrecognized or malformed command",
  "ms": 12345
}
```

Status frame shape:

```json
{
  "type": "status",
  "status": "ok",
  "protocol_version": 1,
  "firmware_version": "FW: 3.0.9",
  "grams": 25.66,
  "ms": 12345,
  "battery_percent": 82,
  "battery_voltage": 3.95,
  "charging": false,
  "timer_running": true,
  "timer_seconds": 12,
  "display_on": true,
  "low_power": false,
  "soft_sleep": false,
  "events_enabled": true,
  "rate_hz": 10,
  "interval_ms": 100
}
```

For backwards compatibility, WiFi only sends weight snapshots by default. A
client must send `events on` before local scale button presses or power-off
notifications are emitted (status frames remain on-request regardless of the
`events` flag). The event stream resets to off when the WebSocket disconnects.

Button event fields:

```json
{
  "type": "button",
  "button": "circle",
  "button_number": 1,
  "press": "short",
  "press_code": 1,
  "ms": 12345
}
```

Button numbers are `1 = circle` and `2 = square`. Press codes currently emitted
over WiFi are `1 = short` and `2 = long`; the current finger-detection path only
emits short-press events.

Power event fields:

```json
{
  "type": "power",
  "event": "power_off",
  "reason": "low_battery",
  "reason_code": 3,
  "ms": 12345
}
```

Power reason codes are `0 = disabled/failed`, `1 = circle double-click`,
`2 = square double-click`, `3 = low battery`, and `4 = gyro` when gyro support
is compiled in.

Power and display command semantics:

- `display on` / `display off`: OLED power save on/off. The scale and WiFi stay
  awake.
- `low_power on` / `low_power off`: sets OLED contrast to minimum/maximum. It
  does not disable the WiFi modem or drop the WebSocket link.
- `soft_sleep on`: turns off the OLED and sensor power rails and pauses normal
  scale-loop work. WiFi remains configured so a later WebSocket command can wake
  it, but weight snapshots stop while soft sleep is active.
- `soft_sleep off` / `soft_sleep wake`: restores sensor power, OLED power, and
  normal scale-loop work.
- `power off`: full scale power-off. The WebSocket link will drop and cannot
  wake the scale.

A scriptable regression test for this protocol lives in
[`tools/ws_feature_test.py`](tools/ws_feature_test.py): with WiFi enabled on the
scale, `pip install websocket-client` then `python3 tools/ws_feature_test.py`
(it auto-discovers `hds.local`, or pass an IP).


# How to upload Web apps?

In order to build and upload Web apps to HDS, you need to use `pio` to build and upload the filesystem image to the Esp32s3.  
Simply run `pio run -t buildfs -t uploadfs` with the Esp32s3 connected to your computer

# Development workflow

Trunk-based: `main` is the single long-lived branch and is always releasable. Do work on short-lived feature branches and merge into `main` once CI passes.

Releases: tag a known-good commit on `main` with the version (`vX.Y.Z`), then run the "Release firmware" workflow with that tag. The workflow publishes the legacy ZIP plus machine-readable OTA assets:

- `firmware.bin`: raw OTA firmware image.
- `littlefs.bin`: raw filesystem image. WiFi OTA releases always publish and require it so skipped versions and test builds with unknown filesystem state are brought back to the production filesystem.
- `manifest.json`: release metadata downloaded by the scale before installing.
- `manifest.sig`: detached SHA-256 signature. The workflow requires `HDS_OTA_SIGNING_KEY_PEM` as a repository secret and `HDS_OTA_MANIFEST_PUBLIC_KEY_PEM` as a repository variable.

The scale checks `https://github.com/decentespresso/openscale/releases/latest/download/manifest.json`, downloads `manifest.sig`, verifies the detached signature with the public key compiled into the firmware, then verifies `model`, optional `pcb`, `min_from`, chip, environment, flash size, partition schema, filesystem partition size/schema, HTTPS certificate chain, asset URL prefix, asset size, and SHA-256 before writing firmware. Manifest `version` and `min_from` must be stable numeric `major.minor.patch` values. The workflow builds a signed catalog from `v3.1.13` upward by merging the previous latest stable signed manifest with the new release, deduping compatible entries, and sorting newest to oldest. The WiFi updater lists compatible stable releases except the firmware version already running.

When `littlefs.required` is true, OTA is staged. The current firmware stores the LittleFS asset metadata in NVS, writes only `firmware.bin` to the inactive app OTA slot, and reboots. The new firmware boots with the same NVS-stored WiFi credentials, passes the normal setup checks, marks itself valid so it owns recovery, then downloads `littlefs.bin`, writes it to the data partition, verifies the raw partition SHA-256 plus partition size/schema and LittleFS mount, clears the pending state, shows completion, and reboots. If WiFi or filesystem update fails, the pending LittleFS state remains in NVS so the next boot or WiFi Update menu can retry. Set `HDS_RELEASE_MIN_FROM` as a repository variable to raise the oldest version that can self-update.

This is safer than writing LittleFS before firmware because old firmware does not intentionally boot with a new filesystem. It is still not the same as A/B data partitions: a power loss during the LittleFS write can leave the single filesystem partition incomplete, but the new firmware has already been marked valid and will retry the pending filesystem update instead of rolling back to old firmware with a changed filesystem.

# Automated builds

With version 3, we have also started using GitHub actions to build the firmware and filesystem images. You can find the recent builds on the "Actions" tab on the GitHub page of this project. From there you can also download the firmware files (e.g. for development builds).


# How to upload firmware files
Web USB Flash(please use Chrome/Edge, Safari or Firefox is not supported):<br />
https://adafruit.github.io/Adafruit_WebSerial_ESPTool/ <br />
The offset values are:<br />
bootloader.bin 0x0000<br />
partitions.bin 0x8000<br />
firmware.bin 0x10000<br />
littlefs.bin: 0x670000<br />

![binary offsets](./assets/esp-tool.png)


This tool works great, but need to reset by pressing the button on the PCB.<br />
And as it erase the eprom, a calibration is also required.<br />

Or use OpenJumper™ Serial Assistant, link as below.(In Chinese)<br />
https://www.cnblogs.com/wind-under-the-wing/p/14686625.html <br />
