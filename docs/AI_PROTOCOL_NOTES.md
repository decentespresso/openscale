# AI Protocol Notes

Read this when changing Decent binary commands, BLE or USB framing, shared response packets, compatibility behavior, or the WebSocket control surface.

## Sources

- Shared Decent packet helpers, response builders, command lengths, and dispatch: `include/decent_protocol.h`.
- BLE GATT transport and `BleDecentCommandSink`: `include/ble.h`.
- USB buffering, text/binary separation, and `UsbDecentCommandSink`: `include/usbcomm.h`.
- WebSocket text and JSON protocol: `include/websocket.h`; its user-facing wire contract is in `README.md`.
- ADS debug host helpers and decoders: `tools/ads_debug_protocol.py`, `tools/decode_ads_debug.py`.

WebSocket is not a wrapper around the Decent binary dispatcher. Keep its backward-compatible JSON/text behavior separate while routing equivalent hardware actions through the same runtime helpers where practical.

## Binary Command Flow

BLE and USB both route Decent commands through `handleDecentBinaryCommand()`. The shared dispatcher owns command validation and meaning; transport sink methods own delivery and transport-specific side effects.

- BLE accepts one GATT write on service `fff0`, write characteristic `36f5`, and notifies responses on `fff4`.
- USB shares serial with text commands and logs. It buffers up to 160 bytes, separates text from frames beginning with `0x03`, and uses a 30 ms timeout to resolve incomplete legacy frames.
- `decentCommandFrameLength()` is the USB stream boundary contract. Update it whenever a binary command length changes.
- Every command handler must guard the bytes it reads with `decentRequireLength()`.

Do not duplicate command interpretation in BLE and USB. Add shared behavior to the dispatcher and only add sink methods when the transports genuinely perform the effect differently.

## Framing And Checksums

Decent binary frames begin with model byte `0x03`. Checksums are XORs of the preceding bytes.

- `0x0F` tare is a seven-byte checksummed command; an invalid checksum must not tare.
- `0x0A`, `0x0B`, `0x25`, and `0x26` have legacy short-form compatibility in the USB length resolver. Do not make checksums universally mandatory without checking existing clients.
- A short USB form is accepted after the receive timeout; a complete valid checksummed form takes precedence.
- Shared seven-byte response builders calculate byte 6 from bytes 0 through 5.
- The `03 0A` LED response stores weight in bytes 2-3, battery state in byte 4, firmware major in byte 5, and checksum in byte 6. The firmware byte must be assigned before calculating the checksum.
- Weight packets use command `0xCE` and signed tenths of a gram, clamped to the `int16_t` range.

ADS debug responses are separate fixed formats: the debug packet is 41 bytes and the reset response is 5 bytes. Keep their Python helpers, decoder, and firmware builders aligned.

## Runtime Effects

Shared packet meaning does not imply identical execution context:

- BLE sink actions that touch display, power, timer, or sleep state generally queue pending work for the main loop.
- USB sink actions run through the USB path and currently perform some effects directly.
- AsyncTCP WebSocket callbacks must only update visible state, reply to their client, or queue main-loop work. Never move I2C, SPI, OLED, power-gating, or blocking work into them.
- Remote tare paths should converge on `requestRemoteTare()`.
- Soft wake paths should converge on `wakeScaleFromSoftSleep()` and remain idempotent.

Preserve response ownership: BLE replies notify `fff4`; USB replies use `Serial.write`; WebSocket replies go only to the requesting client unless an event is explicitly broadcast and heap-gated.

## Compatibility Checklist

- Search both sinks and every response builder before changing a command.
- Preserve exact command lengths, legacy short forms, checksum coverage, and byte order unless a protocol version transition is planned.
- Keep BLE and USB response payloads identical when they use the same shared builder.
- Do not add a WebSocket `type` field to weight snapshots; clients use its absence to identify legacy snapshot frames.
- Keep the legacy WebSocket text `tare` command silent; JSON tare returns a status response.
- Treat `03 0A` as compatibility-sensitive because it covers display, power, low-power, heartbeat, soft-sleep, and the LED response.

## Checks

Use the smallest relevant set:

```sh
python tools/test_ads_debug_protocol.py
python tools/test_decode_ads_debug.py
python tools/test_soft_sleep_ads_wake.py
pio run -e esp32s3
```

Device checks are still required for transport timing and delivery: use `tools/ble_scale_client.py` for BLE heartbeat behavior, `tools/usb_rate_check.py` for USB framing/rate behavior, and the matching ADS monitor for debug packets. There is no complete host-side test for the shared Decent dispatcher, so command-length or dispatch changes need focused coverage rather than relying only on a successful build.
