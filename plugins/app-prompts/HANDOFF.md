# John: App Prompts Test Package

This test build adds short Bluetooth instructions below HDS's live weight.
Keep your cocktail website's design and recipes. Only its maker JavaScript
and one new helper module need to change.

## Install The Firmware

1. Extract the handoff ZIP. Open https://decentespresso.github.io/hds-updater/
   in desktop Chrome or Edge, with the scale connected by a data-capable USB cable.
2. Load `firmware/HDS_FW_app-prompts_hds21_20261019.zip` from the extracted folder.
   Do not load the outer handoff ZIP or select an official release in its place.
3. Leave **full-flash erase disabled**, select the scale's serial port, then flash.
   Keep USB and power connected until the updater finishes.
4. Restart the scale if needed. Close the updater's serial connection before
   connecting another application.

Use this package for HDS 2.1 with the ESP32-S3/V8_1 firmware configuration and
the standard HDS 8 MiB partition layout. Do not install it on another hardware
revision without checking its pin configuration. The updater checks the chip
and flash layout, not the scale's physical hardware revision.

The firmware ZIP contains exactly `bootloader.bin`, `partitions.bin`,
`firmware.bin`, and `littlefs.bin` at its root. With full erase off, the current
updater preserves NVS settings and calibration, writes the firmware to OTA 0,
and clears OTA selection data so that image boots. It replaces the bundled
LittleFS web assets. Full erase would also remove settings and calibration.

This is an unsigned development test package, not an official release or a
signed OTA update. Check its SHA-256 values against `SHA256SUMS.txt` and
`firmware/build-info.json`. `firmware.bin` reports base version `3.1.15`; detect
App Prompts through the capability characteristic, not the version label.
The selection retains WiFi, mDNS, WebSocket, the default on-scale web apps,
ElegantOTA, and Pull OTA. An official firmware update will remove the plugin.

## Update Your Cocktail Maker

The patch targets the live `/cocktails/js/scale_maker.js` fetched on 2026-10-19.
Its original SHA-256 is
`2d80079397244b195de7b87e4c8940d2ae47cc3e2e51528f036f11c58c954ee0`.
It preserves per-ingredient tare, manual Next, serving/garnish ordering, batch
scaling, images, translations in the browser, and the final facts panel.

From your website repository root, with the extracted package available:

```sh
git apply --check /path/to/handoff/webapp/cocktail-maker.patch
git apply /path/to/handoff/webapp/cocktail-maker.patch
```

Copy `webapp/cocktails/js/hds-app-prompts.mjs` into your site's
`cocktails/js/` directory beside `scale_maker.js`. Serve it as JavaScript from
the same HTTPS origin. The patch dynamically imports it; no page or CSS changes
are required. Update your existing asset-cache version if your deployment caches
the maker script. Test on a staging copy before production.

The package also contains the patched `webapp/cocktails/js/scale_maker.js` as a
reference. Use it as a replacement only if your original matches the hash above.
If `git apply --check` fails, port the changes to your current file rather than
overwriting newer work. `webapp/original-scale_maker.js` records the exact baseline.

## What The Changes Do

- Discover App Prompts after the existing BLE weight subscription starts.
- Show `Place shaker; tare`, ingredient/action/quantity prompts, the serving
  instruction, and any garnish steps in their existing order.
- Clear on Done and when leaving the maker. Retain the site's existing
  connection and heartbeat when closing the maker.
- Serialize prompt, tare, and heartbeat writes through one GATT queue.
- Keep the browser-only workflow with older firmware, a missing helper module,
  or a failed prompt write. Disconnect if prompt cleanup keeps failing, rather
  than keeping an old instruction alive with heartbeats.

Prompts accept 1-18 printable ASCII characters. The helper shortens ingredient
labels to fit while keeping the quantity. Send `showPrompt('Gin')` to show only
the ingredient name. For a different wording, edit the `prompt(...)` calls in
`scale_maker.js`; recipes and step progression stay in your app.

The capability read returns `[1, 18, 10]` at characteristic
`8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01` in service `FFF0`.
Show is `[1, ...ASCII]`; clear is `[0]`. Keep the existing four-second heartbeat.
Use `prompts.send(...)` for tare and heartbeat. `showPrompt()` returns delivery
success, not a display readback; priority screens and local cancellation can
suppress the text. See `PROTOCOL.md` for the helper API and recovery behavior.

## Test Before Deployment

1. Make a complete drink in desktop or Android Chrome, including a counted
   ingredient and garnish. Check the scale after each step and verify tare,
   Prev/Next, final totals, and the browser's original UI.
2. Check Clear, Done, maker close/reopen, BLE disconnect/reconnect, and loss of
   heartbeat. Firmware may disconnect after five seconds; prompt expiry is ten.
3. Hold Circle while a prompt is active. It cancels prompts without taring;
   reconnect before expecting prompts again. Check both display rotations and
   timer continuity. Menus, calibration, charging, errors, and OTA keep priority.
4. Connect stock firmware without the plugin and confirm the website still works.

We flashed and booted the prototype on HDS 2.1 and observed Android Chrome's
plugin detection and live-weight updates in the separate diagnostic app.
That is not a complete recipe test of this website integration or a physical
test through hds-updater. The package metadata records the exact firmware bytes.

## Source And Checks

Use the draft branch to edit the plugin:
https://github.com/decentespresso/openscale/tree/odev/app-prompts

- Firmware patch: `plugins/app-prompts/patches/main.patch`.
- Browser helper: `plugins/app-prompts/browser/hds-app-prompts.mjs`.
- Website patch: `plugins/app-prompts/examples/cocktail-maker.patch`.
- Regression check: `python tools/test_app_prompts.py` from the firmware repository.

The optional `diagnostic/` app in this package is a BLE test tool, not a
replacement cocktail website. From the extracted package, serve it with
`python -m http.server 8765 --bind 0.0.0.0 --directory diagnostic` and open
`http://localhost:8765/test/`. `PROTOCOL.md` includes the USB-tablet forwarding
instructions; plain LAN HTTP cannot use Web Bluetooth.
Chrome's Bluetooth chooser does not authenticate the
BLE client to HDS; these advisory prompts are not safety controls for chemical
mixing or dosing.
