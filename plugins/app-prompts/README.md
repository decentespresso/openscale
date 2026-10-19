# App Prompts

Show instructions from a connected Bluetooth app below HDS's native live weight.
The browser owns the recipe, tare timing, and step progression. HDS reports its
native tared weight and does not calculate ingredient targets or grams remaining.

## Installation

After publication, select **App Prompts** in the custom-build configurator and
install the resulting firmware through the custom-build installation flow.
For the current test package, follow `HANDOFF.md`. The plugin is optional,
supports `main`, and requires no WiFi, web apps, or filesystem assets.

For local development, apply the approved patch to a clean matching `main`
checkout and use its dedicated validation environment:

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path $PWD '.pio-core'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
git apply --check plugins/app-prompts/patches/main.patch
git apply plugins/app-prompts/patches/main.patch
pio run -e esp32s3-app-prompts
```

Ensure `.pio-core` is ignored. Do not run simultaneous builds against that core.
The gate `HDS_ENABLE_APP_PROMPTS` defaults off in the patched stock environment
and on in a patched custom build. The dedicated environment enables it explicitly.
Unpatched builds contain no plugin code.

The custom-build service applies approved patches before compilation and handles
signed OTA packaging. Its local `build_custom_firmware.py` publication path
requires a trusted OTA signing key; the validation environment above needs no
signing key and does not publish. `examples/custom-build.json` selects BLE/USB-only
firmware plus this plugin. For a WiFi-enabled custom build, keep the desired
existing features and plugins and add `app-prompts` to the selection.

## Behavior

- A short instruction below the weight replaces the visible timer, without changing the stopwatch.
- Weight, battery, and connection indicators remain visible.
- Hold Circle to cancel prompts for the current BLE connection. This consumes
  the press without taring; later prompts remain blocked until reconnection.
- Clear the prompt when the workflow completes. Disconnecting or losing the app
  heartbeat also restores the normal screen.
- Calibration, overload, ADC errors, menus, charging, and OTA retain priority.
- Prompt state is transient; nothing is saved to NVS.

## BLE Protocol V1

Use the existing service `0000fff0-0000-1000-8000-00805f9b34fb`.
The new read/write characteristic is
`8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01`.
Legacy `36F5` commands and `FFF4` notifications do not change.

Read the characteristic before using it. It returns three bytes:

| Byte | Value | Meaning |
| --- | --- | --- |
| 0 | `01` | Protocol version 1 |
| 1 | `12` | Maximum text length: 18 bytes |
| 2 | `0A` | Heartbeat lease: 10 seconds |

Write with response:

- Clear: the single byte `00`.
- Show: `01` followed by 1-18 printable ASCII bytes (`20` through `7E`).

No terminator, model byte, or checksum is appended. The longest write is 19
bytes, within the default ATT MTU. Empty messages, other operations, controls,
non-ASCII bytes, and oversized payloads cannot replace the prompt.

Keep sending the existing heartbeat to `36F5` every four seconds:
`03 0A 03 FF FF 00 0A`. Other commands and prompt writes do not renew the lease.
The initial connection grants ten seconds. An expired prompt does not reappear
when a late heartbeat arrives; send a new prompt after renewing the lease.
HDS's existing five-second BLE-disconnection policy still applies when enabled.

A GATT acknowledgement confirms delivery, not visibility. Priority screens or
local cancellation can suppress prompts. A capability read does not report the
current displayed text or cancellation state.

## Browser Helper

Host `browser/hds-app-prompts.mjs` on the application's HTTPS origin. Connect
from a user gesture in a browser with Web Bluetooth. Start the existing `FFF4`
weight notifications before creating the helper, then use:

```javascript
import { connectAppPrompts } from './hds-app-prompts.mjs';

const prompts = await connectAppPrompts(server);
await prompts.showPrompt('Add Gin: 35g');
await prompts.clearPrompt();
```

`connectAppPrompts()` sends an initial heartbeat before probing optional prompt
support. Discovery and the capability read share a 2.5-second deadline and never
overlap other GATT operations. On timeout, the helper invalidates its queue,
disconnects, and rejects; reconnect rather than continuing on that connection.
A timeout alone cannot cancel an outstanding Bluetooth request. Missing or
incompatible support still returns a helper for the browser-only workflow.

Use `prompts.send(bytes)` for all existing tare/heartbeat writes, so they share
one queue with prompt writes. `supported` is false when the characteristic is
missing or its version is incompatible. `showPrompt()` and `clearPrompt()`
return false on unavailable support or a prompt transport failure. Invalid text
rejects with `RangeError`. Send `showPrompt('Gin')` to display just the ingredient
name under the weight, or include the action and target as in the example.

After a prompt transport failure, the helper disables further shows for that
connection but retains clear-only recovery. `clearPrompt()` can retry even when
`supported` is false. The helper attempts cleanup after a failed show and before
forwarding a heartbeat if cleanup remains pending. If that retry also fails,
`send(heartbeat)` rejects without sending the heartbeat. Handle that rejection by
disconnecting, as the website patch does, so stale instructions cannot remain
alive through continued heartbeats. Other legacy command writes remain available.

The helper copies outgoing command buffers before queuing them. Disconnecting
invalidates the helper and queued work, even if the same GATT server reconnects.
Create a new helper per connection. When leaving a workflow, clear its prompt.
If the app keeps the connection and heartbeat running, retain that helper for
queued writes and reuse it on return. Call `dispose()` when retiring the helper.
Cancel pending connection setup on exit so a late capability read cannot start
a hidden prompt session.

`ingredientPrompt(action, ingredient, quantity)` shortens the ingredient label
to fit while preserving the quantity. It rejects an action/quantity combination
that cannot fit. Use short English labels for this ASCII-only version.

## Cocktail Integration

`examples/cocktail-integration.mjs` shows the helper calls for placing a glass,
weighed and counted ingredients, serving, completion, and exit.

`examples/cocktail-maker.patch` targets
`/cocktails/js/scale_maker.js` downloaded on 2026-10-19. The unchanged snapshot
in `tests/fixtures/scale_maker.js` lets the checks apply and execute that patch
without network access. The site's owner must review and deploy it; this
repository has no access to the website's source repository or deployment.

From the website repository root:

```sh
git apply --check /path/to/app-prompts/examples/cocktail-maker.patch
git apply /path/to/app-prompts/examples/cocktail-maker.patch
```

Publish `browser/hds-app-prompts.mjs` as `/cocktails/js/hds-app-prompts.mjs`,
with a JavaScript MIME type. The patch loads it after the user selects a device
but before opening the BLE connection. A missing module, unsupported firmware,
or prompt failure leaves the browser-based recipe workflow available.

The patch retains auto-reconnection, ingredient pictures, per-step browser
readouts, per-step tare, manual Next, batch scaling, Prev/Next, garnish ordering,
and the final facts panel. HDS shows its native weight, including the site's
existing per-step tare commands. The plugin adds no relative-weight calculation.
The tare settling window starts only after its queued write finishes. Moving to
another step or closing/reopening the maker invalidates late tare completions.
Non-weighed steps remain manual. The scale shows
`Place shaker; tare` before ingredients and `Mix, then serve` at the serving
step; the browser retains the complete method.

Closing the maker clears its prompt while keeping an established BLE connection
and heartbeat alive, matching the current website. Closing during device
selection or connection setup cancels that attempt. A timed-out auto-connect
attempt cannot replace a newer connection. Pending tare transitions and failed
heartbeats from an old session cannot restart or disconnect the current workflow.

## Verification

```sh
python tools/test_app_prompts.py
python tools/test_plugin_catalog.py
python tools/test_custom_build_execution.py
```

The focused check applies the actual plugin patch to the resolved `main` source,
compiles its state and runtime logic with the installed C++ compiler, and runs
browser-helper and patched-site tests with Node. It covers malformed writes,
ASCII/length bounds, expiry and
timer rollover, cancellation, same-handle reconnection, legacy-firmware fallback,
write serialization, transport failures, and stale queued browser operations.
Runtime checks inject a heartbeat update before lock acquisition to verify
timestamp ordering. Patched-site checks cover a complete recipe, counted steps,
batch quantities, Prev/Next, close/reopen, late device selection and GATT setup,
delayed and unresolved discovery/reads, initial-heartbeat ordering, safe retries,
auto-connect timeout, tares delayed behind prompts, stale tare completions, and
late heartbeat failures. They also cover a displayed prompt followed by failed update
and cleanup writes, later recovery, completion/close, and persistent cleanup
failure while heartbeats continue. These checks use mocked DOM and BLE APIs,
not physical HDS. The current-site fixture also covers manual progression after
a dose reaches its target, per-step tare, serving before garnishes, and the
browser's final drink-weight tally.

The BLE-only prototype built against `main` commit `7f425e5` adds 2,792 bytes of
reported firmware flash and 32 bytes of static RAM versus the same build without
the plugin. The manifest reserves 8 KiB flash and 256 bytes static RAM. These are
build-time figures, not measurements of the BLE characteristic's dynamic heap.
The patched firmware also builds with WiFi, web apps, Pull OTA, grinder support,
and the energy menu enabled together.

Before publishing, validate on physical HDS with desktop and Android Chrome:
both rotations; negative, large, and overload weights; stopped/running timer;
Circle cancellation with connected-button functions both enabled and disabled;
menus, calibration, ADC recovery, charging, sleep, and OTA; connection churn;
and a complete recipe including counted ingredients and Prev/Next.

## Security And Limits

Chrome's permission chooser does not authenticate a client to HDS. This plugin
inherits the existing unauthenticated BLE connection and adds advisory text only,
not calibration, power, or dosing-control commands. Nearby BLE clients could
send misleading prompts. Commercial chemical-mixing use needs a separate
security and safety assessment.

USB prompts, graphics streaming, translations, ingredient-relative weighing,
and scale-button recipe navigation are outside v1. Dynamic BLE heap overhead and
physical OLED readability require device measurements.

## Local Hardware Test App

`browser/test/` is a standalone hardware test app using the same browser helper.
It provides live cumulative weight, explicit tare, custom ASCII prompts,
clear, a five-step recipe with garnish and serving steps, and a heartbeat toggle.
Connecting sends the current recipe instruction but never automatically tares.
The "Last sent" field reports an accepted write, not an OLED readback; look at
the scale to verify placement, readability, cancellation, and priority screens.

Serve only the browser directory, not the repository or build/flash files:

```powershell
python -m http.server 8765 --bind 0.0.0.0 --directory plugins/app-prompts/browser
```

Open `http://localhost:8765/test/` in Chrome. Another device on the same network
can preview `http://<PC-LAN-IP>:8765/test/`, but that HTTP origin cannot use
Web Bluetooth. Do not disable browser security to bypass this requirement.

For a USB-connected Android tablet with authorized USB debugging:

```powershell
adb reverse tcp:8765 tcp:8765
adb shell am start -a android.intent.action.VIEW -d http://localhost:8765/test/ -p com.android.chrome
```

The tablet's Chrome runs the app and uses the tablet's Bluetooth; the PC only
serves files. Select the scale in Chrome's Bluetooth chooser. Wireless-only use
requires HTTPS with a certificate the device already trusts. iPhone Safari does
not provide Web Bluetooth.

The heartbeat toggle pauses only the legacy heartbeat, not prompt validation.
Existing firmware may disconnect after five seconds, before the plugin's
ten-second lease expires. Disconnecting or closing the page clears the scale's
session; timer, rotation, and Circle behavior still need physical observation.

With Playwright available on Node's module path and the server running:

```sh
node plugins/app-prompts/tests/local-app.cjs
```

This check mocks Bluetooth and covers the test app's recipe, tare, live-weight
decoding, input validation, fallback, failed cleanup, serialized writes, and
desktop/mobile layout. It does not replace testing with an actual scale.
