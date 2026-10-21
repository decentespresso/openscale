# John: HDS App Text Test Package

This package contains test firmware and changes for your existing cocktail maker.
It keeps your website's design and recipe flow. The separate diagnostic app is
for checking BLE and the OLED; it is not a replacement cocktail website.

## Install Firmware

1. Extract the package. Open https://decentespresso.github.io/hds-updater/ in
   desktop Chrome or Edge and connect HDS 2.1 with a data-capable USB cable.
2. Load the inner `firmware/HDS_FW_app-text_hds21_20261021.zip`, not this outer ZIP.
3. Leave full-flash erase Off. Select the scale's port and flash. Keep power
   connected until the updater finishes, then close its serial connection.
4. On HDS, enable **Display > App Text** before connecting your browser.

The package targets HDS 2.1, ESP32-S3/V8_1, and standard HDS 8 MiB partitions.
Do not install it on another board revision without checking the pin configuration.
The updater validates the chip and partition layout, not the board revision.
Full erase Off preserves NVS settings and calibration. Flashing replaces LittleFS
and resets OTA selection to boot the supplied image. This is an unsigned test
build, not a signed OTA update or an official firmware release.

App Text defaults Off and remembers the local setting across boots. No remote
command can turn it On. Displayed text remains transient. Clear restores the
normal screen without disabling the setting or resetting the timer.

For cocktail testing, leave both Pressensor and Grind by Weight Off (integration
None), with App Text On. Active Grind by Weight keeps its existing tare-to-arm
behavior, including tare commands from the browser. App Text does not suppress
dosing.

## Update The Website

Review `webapp/build-info.json` for the exact `deweb` baseline commit and script
hash. From a checkout matching that baseline:

```sh
git apply --check /path/to/handoff/webapp/cocktail-maker.patch
git apply /path/to/handoff/webapp/cocktail-maker.patch
```

The patch adds `cocktails/js/hds-app-prompts.mjs` beside `scale_maker.js`.
Serve it from the same HTTPS origin with a JavaScript MIME type. Refresh your
asset-cache version when deploying the changed script. Use a staging copy first.

The package includes the original and revised script for comparison. Replace the
script directly only when the original hash matches. Otherwise port the changes
to your current source instead of overwriting newer work. Run the supplied Node
regression check from your website checkout:

```sh
node --experimental-vm-modules cocktails/tests/app-text.mjs
```

The integration preserves per-step tare, manual Next, serving/garnish order,
batch quantities, final totals, and the fake-scale/agent APIs. It discovers BLE
support without delaying the first heartbeat, serializes text/tare/heartbeat
writes, and starts tare settling after its queued write finishes. Older or
disabled firmware continues with browser-only instructions. Reconnect after
changing App Text on the scale.

## Validate On Hardware

- Make a full recipe, including counted ingredients and garnish. Check each
  scale instruction, per-step zero, Prev/Next, and the final drink total.
- Check Off/On, reboot persistence, Clear, completion, close/reopen, connection
  loss, expired heartbeat, and Circle cancellation. Check both display rotations
  and a running timer. Confirm priority screens keep their controls.
- If both integrations are included, selecting Pressensor or Grind by Weight
  saves the exclusive choice and restarts. App Text remains independent. A lost
  or pending grinder stop must block switching until shutdown is confirmed.

`SHA256SUMS.txt` covers the package contents. `firmware/build-info.json` records
the source, selections, and binary hashes. The package validators check ZIP
layout, binary images, offsets, hashes, and patch/replacement agreement. Physical
OLED, recipe, and updater-flashing validation remain separate checks.

The HDS changes belong to the firmware PR; the cocktail changes belong to
`deweb`. Neither this package nor the PR deploys your website or publishes a
firmware release. See `PROTOCOL.md` for the BLE contract and diagnostic app.
