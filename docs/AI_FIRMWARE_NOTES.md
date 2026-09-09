# AI Firmware Notes

Read this when changing firmware behavior, transports, sleep, calibration, web serving, or shared state. Skip it for web-app-only or release-only tasks.

## Project Shape

This is Half Decent Scale firmware for ESP32-S3 hardware with a load-cell amplifier, OLED, BLE, WiFi, and LittleFS-hosted web apps. PlatformIO builds an Arduino sketch, but most implementation code lives in `include/*.h`, not in `.cpp` files.

Treat the firmware as a unity-style build. Do not include implementation headers from extra translation units unless you have checked for duplicate definitions.

Core files:

- `src/hds.ino`: `setup()`, `loop()`, buttons, scale output, OLED helpers.
- `src/wifi_setup.cpp`: STA/AP setup, NVS credentials, mDNS, reconnect supervision.
- `include/config.h`: board and feature selection.
- `include/parameter.h`: global state.
- `include/declare.h`: shared object instances.
- `include/decent_protocol.h`: shared Decent protocol helpers.
- `include/ble.h`: BLE GATT server and Decent protocol parser.
- `include/usbcomm.h`: USB protocol and ADS debug packets.
- `include/webserver.h`: AsyncWebServer, LittleFS routes, websocket object.
- `include/websocket.h`: `/snapshot` WebSocket protocol and pending-command deferral.
- `include/power.h`: battery monitoring, shutdown, deep sleep teardown.
- `include/menu.h` and `include/display.h`: OLED UI and menu actions.
- `include/finger_detection.h`: touch-button press classifier.

## Threading Model

The Arduino sketch runs on the main loop task. `websocket.onEvent(...)` runs on the AsyncTCP task, potentially on another core.

Unsafe from AsyncTCP callbacks:

- `u8g2.*`
- power-gating via `digitalWrite(PWR_CTRL, ...)` or `digitalWrite(ACC_PWR_CTRL, ...)`
- `stopWatch.*`
- I2C, SPI, OLED, sensor, or blocking hardware work

Allowed from AsyncTCP callbacks:

- state changes protected by the affected subsystem's existing critical section, queue, or other synchronization
- `Serial.print*`
- `client->printf`
- `websocket.printfAll`, when heap-gated for broadcasts

`volatile` can be appropriate for hardware registers, ISR-visible flags, or an existing API contract, but it does not provide atomicity, memory ordering, or task synchronization.

The WebSocket callback updates visible state and queues hardware work. `loop()` drains pending work at the top via `processWsPendingCmds()`, before the soft-sleep guard, so queued wake and shutdown work can still run.

Use `wsQueuePending(bits)` for single actions. Use `wsReplacePending(set, clear)` for mutually exclusive pairs such as display, low power, sleep, and timer commands.

## WiFi And BLE

WiFi and BLE share the 2.4 GHz radio. Keep Arduino-ESP32 default modem sleep. Do not call `WiFi.setSleep(false)`.

Disabling WiFi sleep while BLE is connected can cause packet loss, duplicate pings, and multi-second HTTP stalls.

## ADC Library

The load-cell driver is the external `decentespresso/ADS1232_ADC` dependency pinned by commit in `platformio.ini`. Keep it pinned.

Stay in polling mode. Do not call `scale.beginTask()`. The library task can run on the same core as AsyncTCP and starve `/snapshot`; this firmware calls `scale.update()` synchronously from `loop()` and calibration paths.

`setSamplesInUse()` clears the ring buffer and resets valid samples. After a sensitivity or sample-window change, `getData()` can briefly return `0.0` and then ramp. A physical load step also restarts the smoothing ramp. One-shot measurements after either event must wait until readings plateau for consecutive reads.

Tare averages the current buffer, often one sample in fast mode, so it is noisier than a fixed full-window tare.

The USB ADS debug packet keeps its 41-byte framing and checksum. Byte 24 is the raw reset reason captured at boot. Bytes 25-37 are zero-filled because the current library does not precompute stats.

## Scale-Top Taps

`include/tap_detector.h` processes the existing `f_current_raw_value` after
`pureScale()` in the weighing loop. It does not alter ADS polling or filtering.
The states are Settling, Ready, Rising (including unloading), Valley, and Locked.
Five stable samples at 100 ms intervals arm the first rise. Subsequent rises use
their local valley, not the original baseline; rejected rebounds also start a
new valley so an old undershoot cannot inflate a later peak's prominence.

Thresholds are grouped in `TapDetector`: rises exceed 2 g per observed change.
The first prominence exceeds 10 g; repeats rise more than 6 g from their local
valley and their height above the original baseline exceeds 35% of the strongest
accepted height. Keeping those tests separate admits overlapping taps without
letting a negative undershoot inflate a small ringing peak. A peak unloading by
at least 6 g and 45% of its local prominence arms the next valley. Action completion separately requires
70% unloading of the final peak relative to the original baseline (with a 2 g
floor). This rejects half-released holds without requiring deep unloading
between taps. The relative height floor does not shrink after weaker accepted peaks.

Rise onsets must be at least 50 ms apart. Preserve the original 400 ms wait for
another rise and 600 ms maximum contact duration, including slower gestures.
The wait starts at the confirmed drop and restarts once at final unloading,
not on every cached sample or subsequent fluctuation. A double is emitted on
the first tick beyond that wait, unless another rise is being evaluated. A
held candidate cancels the sequence rather than falling back to tare. Triple
is emitted as soon as the third peak satisfies the final unloading condition;
until then no fourth peak can replace it.
There is no additional 100 ms action delay: unloading is already confirmed and
the double decision already reserves the full third-tap window. Local tare/timer
actions and their enable settings are unchanged, including the existing tare delay.

COM5 hardware capture on 2026-09-09 measured about 10 SPS. A natural double had
401 ms between observed rises; a triple had about 60% initial unloading and
7.6 g second prominence. The earlier 300 ms onset window, 200 ms duration,
70% intermediate unloading, and 10 g repeat floor rejected those signals.
Captured double/triple/held traces and original slower sequences are native
regression tests. Observation timestamps can lag ADC acquisition by one debug
poll interval, so hardware confirmation is still required after changing thresholds.

A later fast series exposed a 48% initial fall and a 7.4 g local repeat rise
while still above baseline; another light first peak fell only 7.2 g. The
6 g / 45% drop and separate local-rise / baseline-height tests cover these
captured traces. A captured 0.64 g second rise remains rejected deliberately:
counting that as a tap would undermine noise and held-finger rejection.

Cached readings create no extra edges. Recognition still requires distinct ADC
peaks and valleys: 10 SPS cannot reliably resolve 50 ms taps, and smoothing can
hide fast taps. Do not change sampling to compensate without a separate hardware
review. Deep pressure modulation or large object bounces indistinguishable from
tap traces remain a hardware-validation risk. Serial `tapd on` enables one
`[TAPRAW] timestamp weight` line per fresh ADC reading in the weighing loop;
`tapd off` disables it. It defaults off, is not persisted, and does not change
sampling or filtering. Prefer this stream over repeated USB debug snapshots,
which can miss intermediate samples. Replay timestamped weight traces in
`test/test_tap_detector/test_main.cpp`.

## Async Web Server

- Register handlers before `server.begin()`.
- `server.end()` does not clear handlers; guard repeat init.
- Do not call `addHandler(&websocket)` twice.
- `LittleFS.begin()` is idempotent.
- Multiple WebSocket clients are supported.
- Shared WebSocket session state resets only when the last client disconnects.
- Broadcast with `websocket.printfAll(...)` or `websocket.textAll(...)`, not a manual `getClients()` loop.
- Gate every broadcast-to-all helper with `wsBroadcastHeapOk()`.

The weight broadcast hot path in `sendWebsocketWeightAll()` must format the complete payload into a bounded stack buffer and call `textAll()`. Do not reintroduce per-frame `printfAll()` formatting allocations; keep the heap gate before the broadcast.

Broadcasts allocate a shared payload and per-client queue entries. `printfAll` also allocates a transient formatting buffer. Arduino-ESP32 builds without exceptions, so `std::bad_alloc` aborts and reboots the device. Connection churn and half-open clients can exhaust heap. Low-heap behavior should skip broadcasts, not allocate.

### WebSocket Heap Deep Reference

Verify the named source constants before changing these values:

- `WS_BROADCAST_HEAP_FLOOR = 32000` in `include/websocket.h`.
- `HEAP_CRITICAL = 15000` in `src/wifi_setup.cpp`.
- `WS_MAX_QUEUED_MESSAGES=8` in `platformio.ini`.
- WebSocket client ACK timeout is 30000 ms in `setupWebsocketEvents()`.

Relevant serial patterns:

- `[ws] low heap ... -> skip broadcast`
- `[ws] low heap ... -> skip client reply`
- `[heap] CRITICAL low free=...`
- `[health] uptime=... heap=...`

Connection churn and half-open clients consume heap through queued per-client messages. The broadcast floor prevents new allocations before the critical heap watchdog must reboot the device.

Only act on complete unfragmented text frames:

```c
if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
  String msg((const char *)data, len);
}
```

## Troubleshooting

| Symptom | First place to look |
| --- | --- |
| Device is pingable but HTTP times out mid-body | AsyncTCP starvation from main-loop work or hardware work moved into the callback. |
| `ping` shows duplicates or BLE has packet loss | Look for `WiFi.setSleep(false)`. |
| Device is unreachable after flash but USB enumerates | WiFi association failed on this boot; reset and retry. |
| Boot logs show `LittleFS mount failed` | Upload the filesystem image; firmware-only flashing does not update LittleFS. |
| Flashing becomes much slower than usual | Firmware may be interfering with bootloader handshake. Treat as a serious firmware bug. |
| Panic or abort under multi-client WiFi load | Check the WebSocket heap constants and serial patterns above for connection churn, skipped broadcasts, and critical heap. |

## Focused Checks

Run only the checks matching the change:

```sh
python tools/test_calibration_validation.py
python tools/test_soft_sleep_ads_wake.py
python tools/test_tap_action_contract.py
pio test -e native -f test_tap_detector
pio run -e esp32s3
```

`tools/ws_feature_test.py` and `tools/ws_command_test.py` require a WiFi-enabled scale.

## Keeping Notes Fresh

Add lessons that would have saved debugging time: new footguns, thread-safety constraints, workflow changes, non-obvious symptoms, and cross-file dependencies. Prune stale claims. Prefer fewer, sharper notes over long background.
