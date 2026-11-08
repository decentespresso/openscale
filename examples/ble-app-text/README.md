# BLE App Text

App Text lets a connected BLE app display an instruction below HDS's live
weight. The upcoming 3.1.16 firmware includes this capability in standard and
custom builds; published 3.1.15 does not. There is no App Prompts plugin to
select. Hosted 3.1.16 custom-build support is staged, not deployed, and the
public configurator still defaults to 3.1.15.

## Enable On The Scale

Open **Display > App Text** and turn it On. A new installation defaults to Off.
HDS remembers this choice across boots, but does not save displayed text.
Changing the switch needs no restart. Off clears the current instruction and
blocks Show requests. On accepts new text without restoring an old instruction.
Reconnect your browser after changing the switch to refresh its capability read.

Text replaces the timer display while the stopwatch keeps running. HDS retains
weight, battery, and connection indicators. Clear, disconnect, or ten seconds
without a valid legacy heartbeat restores the normal display. The existing BLE
watchdog may disconnect after five seconds, before text expiry.

Text that fits stays centered. Longer text starts left-aligned for one second,
scrolls left at ten pixels per second until the final character is visible,
and holds that final view for one second before returning to the beginning.
New text or a newly visible instruction restarts this cycle. Heartbeats and
repeated identical Show requests do not restart scrolling.

Hold Circle while app text is visible to cancel text for that connection without
taring. Reconnect before sending text again. Turning App Text Off and On does
not bypass this cancellation.

Menus, calibration, charging, overload, ADC errors, updates, and sleep retain
priority. Pressensor retains its display while active. The grinder retains its
display and controls while armed, grinding, stopping, awaiting removal/zero, or
reporting an error. Fresh app text can appear on the idle weighing screen.
App Text does not enable, disable, or control either integration.
Tare-to-arm behavior in Grind by Weight still applies, including browser tares.
Select neither integration when using the cocktail maker.

## BLE Contract

Service: `0000fff0-0000-1000-8000-00805f9b34fb`.
Read/write characteristic: `8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01`.
The characteristic remains available while the local setting is Off.

- Capability read: `[1, 128, 10, enabled]`; `enabled` is `0` or `1`.
- Show: `[1, ...ASCII]`, with 1-128 printable ASCII bytes, `0x20` through `0x7e`.
- Clear: `[0]`.
- Heartbeat: the existing `03 0A 03 FF FF 00 0A` packet to `36F5`, every four seconds.

No terminator or checksum follows the text. The largest Show write is 129 bytes.
Send each Show as one acknowledged write; the BLE stack handles long writes.
Scrolling runs locally and does not require repeated browser writes.
Invalid or oversized writes leave the current instruction unchanged. Legacy
weight notifications, tare, and heartbeat packets keep their existing formats.
A GATT acknowledgement confirms transport delivery, not OLED visibility.
Local opt-out, Circle cancellation, and priority screens can suppress a message.
WebSocket and USB text commands are outside this revision.

## Browser Helper

Serve `browser/hds-app-prompts.mjs` as JavaScript from your app's HTTPS origin.
Subscribe to the existing weight notifications before creating the helper.

```javascript
import { connectAppPrompts } from './hds-app-prompts.mjs';

const text = await connectAppPrompts(server);
if (text.enabled) await text.showPrompt('Add Gin: 35g');
await text.clearPrompt();
```

`supported` reports protocol support. `enabled` also requires local opt-in.
`maxPromptBytes` reports the negotiated text limit, up to 128 bytes. Pass that
limit as the fourth argument to `ingredientPrompt(action, ingredient, quantity,
maxPromptBytes)`. Its default remains 18 for older firmware. Larger limits keep
the full name and quantity with a colon and space; an over-limit instruction
is rejected rather than silently shortened. The cocktail maker then displays
its existing "Check recipe step" instruction.
The helper accepts the prototype's three-byte capability response as enabled.
The updated helper also accepts firmware advertising the original 18-byte limit.
Unsupported or disabled firmware keeps the browser-only workflow available.
Use `send(bytes)` for tare and heartbeat so writes share the same GATT queue.
The helper sends an initial heartbeat before optional discovery, gives discovery
and its read 2.5 seconds, and disconnects on timeout. A timeout cannot cancel an
outstanding Bluetooth request. Reconnect instead of issuing concurrent requests.

After a prompt transport failure, the helper stops sending new text and retries
Clear. If cleanup still fails before a heartbeat, `send(heartbeat)` rejects.
Disconnect on that rejection rather than renewing a stale instruction.
Clear on workflow completion and exit; call `dispose()` when retiring a connection.
Recipes, translations, quantities, and step progression belong in the app.

## Combined Integrations

Custom builds based on 3.1.16 or current `main` can include both Pressensor and
Grind by Weight. The 3.1.16 compatibility mapping is staged for later service
activation; it does not add a public firmware option during preparation.
Use either integration's Enabled menu control to select it or select neither.
HDS saves one selection, stops the previous integration, and starts the selected
one without leaving the menu or restarting. You can then select a plug or sensor.
The grinder stays paused until you leave the menu and still requires a user tare
before arming.
Sensor/plug selections and dose tuning remain saved. App Text stays independent.

A pending or unconfirmed grinder stop blocks an integration change. Explicit
power off sends OFF and waits at most 250 ms for confirmation before closing the
connection and entering USB standby or powering down. The plug switches its
relay OFF on disconnect.
An attempted integration or plug change retries the current plug after a latched
stop error. HDS blocks plug selection until that plug's shutdown is confirmed.
Disconnection alone does not prove that the grinder stopped. Older firmware
refs retain their existing build conflicts and patches.

## Checks And Diagnostic App

```sh
python tools/test_app_text.py
python tools/check_app_text_layout.py
```

The native checks exercise scrolling holds, repeat timing, text validation, expiry, cancellation, opt-in, callback
interleaving, integration migration, and restart guards. Browser checks cover
capability negotiation and serialized command writes. They use simulated hardware.
The layout check needs the U8g2 sources from an `esp32s3` build. It checks rendered
pixels and font bounds in both rotations without claiming a physical OLED test.

Serve only the browser directory, not the repository or firmware binaries:

```sh
python -m http.server 8765 --bind 0.0.0.0 --directory examples/ble-app-text/browser
```

Open `http://localhost:8765/test/` in Chrome. For an authorized USB-connected
Android tablet, run `adb reverse tcp:8765 tcp:8765`, then open that localhost URL
on the tablet. Plain LAN HTTP cannot use Web Bluetooth; do not disable browser
security. Wireless use requires HTTPS with a certificate the device trusts.

With Playwright installed and this server running:

```sh
node examples/ble-app-text/tests/local-app.cjs
```

Physical tests must check OLED placement, both rotations, timer continuity,
the complete scrolling cycle, 128-byte writes on Android and Windows, Circle
behavior, priority screens, connection churn, and the real grinder's Off
acknowledgement. Software tests do not establish physical shutdown safety.
BLE clients are unauthenticated. Treat app text as advisory, not as a dosing or
chemical-mixing safety control.
