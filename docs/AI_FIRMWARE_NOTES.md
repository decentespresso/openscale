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

Safe from AsyncTCP callbacks:

- visible single-word flags shared with `loop()`, when declared `volatile`
- `Serial.print*`
- `client->printf`
- `websocket.printfAll`, when heap-gated for broadcasts

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

## Async Web Server

- Register handlers before `server.begin()`.
- `server.end()` does not clear handlers; guard repeat init.
- Do not call `addHandler(&websocket)` twice.
- `LittleFS.begin()` is idempotent.
- Multiple WebSocket clients are supported.
- Shared WebSocket session state resets only when the last client disconnects.
- Broadcast with `websocket.printfAll(...)`, not a manual `getClients()` loop.
- Gate every broadcast-to-all helper with `wsBroadcastHeapOk()`.

`printfAll` allocates per client. Arduino-ESP32 builds without exceptions, so `std::bad_alloc` aborts and reboots the device. Connection churn and half-open clients can exhaust heap. Low-heap behavior should skip broadcasts, not allocate.

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

## Keeping Notes Fresh

Add lessons that would have saved debugging time: new footguns, thread-safety constraints, workflow changes, non-obvious symptoms, and cross-file dependencies. Prune stale claims. Prefer fewer, sharper notes over long background.
