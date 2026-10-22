# BLE App Text

App Text lets a connected BLE app display an instruction below HDS's live
weight. Firmware includes this capability in standard and custom builds.
There is no App Prompts plugin to select.

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

- Capability read: `[1, 18, 10, enabled]`; `enabled` is `0` or `1`.
- Show: `[1, ...ASCII]`, with 1-18 printable ASCII bytes, `0x20` through `0x7e`.
- Clear: `[0]`.
- Heartbeat: the existing `03 0A 03 FF FF 00 0A` packet to `36F5`, every four seconds.

No terminator or checksum follows the text. The largest Show write is 19 bytes.
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
The helper accepts the prototype's three-byte capability response as enabled.
Older or disabled firmware keeps the browser-only workflow available.
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

On updated `main`, custom builds can include both Pressensor and Grind by Weight.
Use either integration's Enabled menu control to select it or select neither.
HDS saves one selection and updates both Enabled rows immediately, but keeps the
current integration running until you exit the menu and the scale restarts.
Only the selected integration runs after boot.
Sensor/plug selections and dose tuning remain saved. App Text stays independent.

A pending or unconfirmed grinder stop blocks the change and is checked again
before restarting on exit. If shutdown becomes unconfirmed after saving, HDS
keeps the saved selection but does not restart. Connection recovery continues
outside the menu; re-enter and exit the menu after the plug confirms Off.
An attempted integration or plug change retries the current plug after a latched
stop error. HDS blocks plug selection until that plug's shutdown is confirmed.
Disconnection alone does not prove that the grinder stopped. Older firmware
refs retain their existing build conflicts and patches.

## Checks And Diagnostic App

```sh
python tools/test_app_text.py
python tools/check_app_text_layout.py
```

The native checks exercise text validation, expiry, cancellation, opt-in, callback
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
Circle behavior, priority screens, connection churn, and the real grinder's Off
acknowledgement. Software tests do not establish physical shutdown safety.
BLE clients are unauthenticated. Treat app text as advisory, not as a dosing or
chemical-mixing safety control.
