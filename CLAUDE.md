# CLAUDE.md — Half Decent Scale firmware

Espresso-scale firmware for the HDS hardware: ESP32-S3 + load-cell amplifier (ADS1232 or HX711) + 128x64 OLED + BLE + WiFi. Built with PlatformIO + Arduino framework. The on-device web app at `/` talks to the firmware over a single `/snapshot` WebSocket.

## Quick reference

```sh
# All commands run from the repo root.
pio run -e esp32s3                                                # build
pio run -e esp32s3 -t upload --upload-port /dev/cu.wchusbserial110 # flash firmware
pio run -e esp32s3 -t uploadfs --upload-port /dev/cu.wchusbserial110 # flash LittleFS (web_apps/)
```

```sh
# Serial monitor — pio device monitor needs a PTY. From a non-tty harness use
# pyserial directly. Path is the PlatformIO-bundled python so pyserial is on
# its sys.path; system python3 typically isn't.
/opt/homebrew/Cellar/platformio/6.1.19_1/libexec/bin/python3 -u -c "
import serial, sys
s = serial.Serial('/dev/cu.wchusbserial110', 115200, timeout=1)
while True:
    line = s.readline()
    if line:
        sys.stdout.write(line.decode('utf-8', errors='replace'))
        sys.stdout.flush()
"
```

The scale advertises mDNS `hds.local` plus a DNS-SD service `_decentscale._tcp` (TXT `path=/snapshot proto=ws model=hds fw=…`) for app discovery. On macOS use `curl -4` or `--resolve` — plain `curl http://hds.local/` blocks for ~5s on the AAAA lookup before falling back to A.

If no WiFi credentials are stored, the device falls back to AP mode: SSID `DecentScale` / password `12345678` / IP `192.168.1.1`. Browse there and POST credentials to `/setup/wifi` (the on-device UI provides a form).

WebSocket protocol regression check (WiFi enabled on the scale): `python3 tools/ws_feature_test.py` (needs `pip install websocket-client`; exits non-zero on failure).

## Code layout

This codebase is unusual: **most logic lives in `include/*.h` as full implementations**, not just declarations. There's only one `.ino` and two `.cpp` files. Don't include the same header from two translation units; treat them as a unity build.

| Location | Contents | Key line refs |
| --- | --- | --- |
| `src/hds.ino` | `setup()`, `loop()`, button callbacks, scale/UI helpers — ~1880 lines | `setup()` ~395, `loop()` ~1265 |
| `src/wifi_setup.cpp` | STA/AP bring-up; credentials in NVS preferences | `connectToWifi`, `setupAP` |
| (load-cell driver) | External lib `decentespresso/ADS1232_ADC` (pinned by sha in `platformio.ini`); the old vendored `src/ADS1232_ADC.cpp` + `include/ADS1232_ADC_CONFIG.h` were removed in #60. `DATA_SET` now lives in `include/config.h`. See "ADC library" footgun below. | |
| `include/config.h` | **Per-board hardware config.** Selects `BUZZER`, `ACC_MPU6050` / `ACC_BMA400`, `ESPNOW`, `FIRMWARE_VER`, etc. Active board is chosen by `#if defined(BOARD_X)` branches. | |
| `include/parameter.h` | All global state declarations — every `b_*`, `i_*`, `t_*`, `f_*` global lives here | |
| `include/declare.h` | Object instances (`u8g2`, `stopWatch`, `scale`, `mpu`, …) | |
| `include/ble.h` | BLE GATT server + decentespresso wire-protocol parser | |
| `include/webserver.h` | `AsyncWebServer` + the `websocket` object; `startWebServer`/`stopWebServer` (handler registration + init ordering) | |
| `include/websocket.h` | **`/snapshot` WebSocket protocol** — control + event + status/error frames, command dispatch, the `wsPendingMask` deferral machinery, per-client broadcast, `setupWebsocketEvents()`. Extracted from `hds.ino` in #54 | `processWsPendingCmds` ~120 |
| `include/usbcomm.h` | USB binary protocol (decentespresso-compatible) | |
| `include/power.h` | Battery monitoring, `shut_down_*`, `esp32_sleep()` — **centralized teardown chain** | `esp32_sleep()` ~166 |
| `include/finger_detection.h` | Touch-button press classifier | |
| `include/menu.h`, `display.h` | OLED UI | |
| `web_apps/` | Static files served via LittleFS at `/` (Quality_Control_Assistant, Weigh_Save, dosing_assistant, index.html) | |

## Threading model — the #1 footgun

ESP32-S3 has two cores. The Arduino sketch runs on the main loop task. **`websocket.onEvent(…)` runs on the AsyncTCP task** — a different task, possibly a different core. Anything touched from both must be safe to share.

| Resource | Safe to touch from AsyncTCP task? | Reason |
| --- | --- | --- |
| `u8g2.*` | **No** — I²C bus, races OLED draws issued from the main-loop task (`loop()`, button callbacks, menu/display helpers) | Defer via `wsQueuePending` |
| `digitalWrite(PWR_CTRL, …)`, `digitalWrite(ACC_PWR_CTRL, …)` | **No** — power-gating must be sequenced with the `u8g2` ops in the SLEEP_ON / SLEEP_OFF paths | Defer |
| `stopWatch.*` | **No** — multi-field (running flag + start ts + accumulator) and also mutated from `loop()`, BLE and USB; a status-frame read can tear a write across tasks | Defer via `wsReplacePending(WSP_TIMER_*)`; `loop()` applies start/stop/zero in `processWsPendingCmds` |
| Single `bool` / aligned `uint32_t` flags shared with `loop()` | Yes — **but mark them `volatile`** | See `include/parameter.h` |
| `Serial.print*`, `websocket.printfAll`, `client->printf` | Yes — library serializes internally | |

The deferral pattern (`include/websocket.h`, drained from `loop()` in `src/hds.ino`):

```c
// AsyncTCP task (WS event callback):
b_u8g2Sleep = true;                                  // visible-state update, atomic
wsReplacePending(WSP_DISPLAY_OFF, WSP_DISPLAY_ON);   // queue hw op, supersede opposite

// loop() — drained at the very top, before the b_softSleep guard, so
// SLEEP_OFF / POWER_OFF queued on the AsyncTCP task can still wake/shut.
processWsPendingCmds();
```

Use `wsQueuePending(bits)` to queue a single action; use `wsReplacePending(set, clear)` for mutually-exclusive pairs (display, low_power, sleep, timer) so a stale opposing bit doesn't survive into the drain. Without this, a fast `off; on` burst can leave the hardware in the opposite state of what `b_u8g2Sleep` reports.

New cross-task globals belong in `include/parameter.h` with `volatile`.

## WiFi / BLE coexistence — the #2 footgun

WiFi and BLE share the same 2.4 GHz radio. The Arduino-ESP32 default is `WIFI_PS_MIN_MODEM` (WiFi sleeps between DTIM beacons), and the BT coexistence layer needs those sleep windows for BLE slots.

**Do not call `WiFi.setSleep(false)`.** Measured impact with BLE connected: ~8% packet loss, multi-second HTTP stalls, the AP retransmitting (`ping` shows `(DUP!)`). The Arduino default is correct.

## ADC library (`decentespresso/ADS1232_ADC`) — polling mode only

Since #60 the load-cell driver is the external `decentespresso/ADS1232_ADC` lib (pinned to a commit sha in `platformio.ini` — keep it pinned; the lib has no semver tags and an unpinned URL gives non-reproducible builds). Header is in the lib's `include/`, sources in `src/`.

- **Stay in polling mode. Do not call `scale.beginTask()`.** The lib can run its bit-bang read on a background FreeRTOS task pinned to **core 1** (prio 2) — the same core as AsyncTCP (`CONFIG_ASYNC_TCP_RUNNING_CORE=1`). Starting it would contend with the WS task and starve `/snapshot`. We drive it synchronously instead: `scale.update()` from `loop()` (~`hds.ino` 935) and from the `menu.h` calibration loops. In polling mode the lib's task never spawns and `update()` does the read on the calling task.
- **`setSamplesInUse()` clears the ring buffer and resets `validSamples=0`.** Right after a sensitivity change (`ble.h`, `usbcomm.h`, `menu.h`, and `setup()`'s `setSamplesInUse(1)`), `getData()` returns `0.0` until the window refills, then ramps up. The old vendored lib backfilled with the last smoothed value, so this transient blip-to-zero on live sensitivity switches is new. `start()` pre-fills via its settle loop, so boot-time tare is fine.
- Tare averages whatever samples are currently in the buffer (often 1 in fast mode), not a fixed `DATA_SET` window — noisier zero than the old lib.
- The USB debug packet (`buildAdsDebugPacket`) keeps its 41-byte framing + checksum. Byte 24 carries the raw `esp_reset_reason()` code captured at boot (`g_resetReasonCode`); bytes 25-37 (formerly min/max/avg/stddev) are zero-filled — the new lib doesn't precompute stats. Host tools plotting those see flatlines, not a framing break.

## ESPAsyncWebServer notes

- `server.begin()` must be called **after** handlers are registered.
- `server.end()` does **not** clear the handler list — guard repeat-init with `static bool handlersRegistered` (see `include/webserver.h`).
- Don't `addHandler(&websocket)` twice. The library silently keeps both.
- `LittleFS.begin()` is idempotent.
- Multiple concurrent WS clients are supported (on-device web UI + a separate app). `cleanupClients()` caps at `DEFAULT_MAX_WS_CLIENTS` (8 on ESP32); shared session state (rate, events) resets only when the last client disconnects (`server->count() == 0`).
- Broadcast with `websocket.printfAll(...)`, not a hand-rolled `getClients()` loop: `getClients()` doesn't take the library's client-list mutex, so iterating it on the loop task races a client disconnect on the AsyncTCP task (use-after-free). `printfAll` holds the lock and sends to each client.
- **`printfAll` allocates per client and throws `std::bad_alloc` when the heap is exhausted** — and Arduino-ESP32 builds `-fno-exceptions`, so the throw goes straight to `std::terminate()`→`abort()`→reboot. Connection churn (many/half-open WS clients lingering under the 30 s ack timeout) can collapse the heap, so every broadcast-to-all helper (`sendWebsocketWeightAll`, `sendWebsocketStatusAll`, button/power-off) is gated by `wsBroadcastHeapOk()` (`WS_BROADCAST_HEAP_FLOOR`, ~25 KB, above the 15 KB heap watchdog): when heap is below the floor the frame is **skipped**, not allocated. Per-client queues are capped via `-D WS_MAX_QUEUED_MESSAGES=8` (lib default 32) so a backed-up client can't hoard heap. Don't add a new `printfAll` broadcast without the `wsBroadcastHeapOk()` guard.

WS frame parsing: only act on complete unfragmented text frames:

```c
if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
  String msg((const char *)data, len);  // NOT += char-by-char (O(N²))
  ...
}
```

## Style conventions

| Prefix | Meaning |
| --- | --- |
| `b_` | bool |
| `i_` | int / count / GPIO pin |
| `t_` | timestamp (millis) |
| `f_` | float |
| `WSP_` | WebSocket pending-action bit |

Functions and locals are camelCase. Some legacy snake_case remains; don't churn it. Timer deltas use `millis() - lastUpdate` with **`unsigned long`** for both — a signed `lastUpdate` wraps incorrectly across the 49.7-day rollover.

## Build / OTA notes

- `git_rev_macro.py` (referenced in `platformio.ini` `build_flags`) injects the current git rev as a compile-time macro.
- `ELEGANTOTA_USE_ASYNC_WEBSERVER=1` is set; `ElegantOTA.loop()` is called in `loop()` and the OTA UI is available when `b_ota` is set.
- `CONFIG_ASYNC_TCP_RUNNING_CORE=1` pins AsyncTCP's worker task to core 1.
- `.gitattributes` enforces LF line-endings repo-wide. A 2026-Q2 commit normalized all files; bisecting across that point will show enormous diffs even for one-line changes.

## When something is broken

| Symptom | First place to look |
| --- | --- |
| Device pingable, HTTP times out mid-body | AsyncTCP task starvation from main-loop work — see `processWsPendingCmds` and the WS event callback. Don't move hardware ops back into the callback. |
| HTTP fast, but `curl` reports ~5s constant delay | macOS resolver waiting on AAAA. Use `curl -4` or `--resolve`. Not a device problem. |
| `ping` shows `(DUP!)` or 8%+ loss with BLE active | Someone re-introduced `WiFi.setSleep(false)`. Revert. |
| Device unreachable after flash, USB still enumerates | WiFi failed to associate on this boot. Reset via RTS toggle (see Quick reference) and retry. Not deterministic; the router can rate-limit fast reconnects. |
| Boot logs show `LittleFS mount failed` | Run `pio run -t uploadfs` to write the filesystem image — firmware-only flashes don't touch it. |
| `pio device monitor` hangs in a non-PTY shell | Use the pyserial snippet in Quick reference. |
| `pio` flash takes >60s instead of ~15s | Bad firmware is choking the bootloader handshake. Symptom of a serious bug on the device (WiFi coex, OLED stuck, etc.), not a hardware fault. |
| `reset_reason=panic` / `abort()` + reboot under sustained multi-client WiFi load | Heap-exhaustion OOM in a WS broadcast: `printfAll` → `operator new` throws `bad_alloc`. Broadcasts are heap-gated (`wsBroadcastHeapOk`); look for `[ws] low heap … skip broadcast` on serial and a falling `[health] heap=`. Driven by WS connection churn (half-open clients lingering on the 30 s ack timeout). Not thermal. |

## Keeping this file fresh

This document is meant to evolve with the codebase. During a session, if you (Claude or human) discover something that would have saved time to know at the start — a new footgun, a thread-safety constraint, a workflow that changed, a file layout shift — update this file as part of the same change, or open a small `docs: CLAUDE.md` follow-up PR if it isn't tied to a code change.

- **Add** lessons earned through debugging, new conventions, recurring symptoms with non-obvious root causes, and cross-file dependencies that aren't visible in `#include` graphs.
- **Don't add** per-feature implementation details (those belong next to the code), transient information, or anything a quick `grep` would surface.
- **Prune** entries that no longer match the code — stale docs mislead worse than missing docs do. Refresh line refs when a file grows or shrinks past obvious anchors.
- When in doubt: prefer fewer, sharper claims over more, vague ones.

If you fix a bug whose symptom is documented in the "When something is broken" table, leave the entry in place — it's still the right "first place to look" for the next person.

## Don't

- Don't call I²C / SPI / blocking IO from the AsyncTCP task.
- Don't force WiFi to never sleep while BLE is active.
- Don't accumulate `String` byte-by-byte from a known-length buffer — use `String((const char*)buf, len)`.
- Don't add new global state outside `include/parameter.h`.
- Don't break the single-line `pio run` and `pio run -t upload` flows by adding required interactive steps.
- Don't `--amend` or `git push --force` on `main`. The PR flow is trunk-based with squash merges via GitHub.
