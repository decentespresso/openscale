# Agent Instructions

Use `docs/AI_REPO_MAP.md` first for orientation. Read broader docs only when the task needs them.

## Always

- Plan with the user before complex or risky operations.
- Security first; performance second.
- Preserve existing user changes. Do not revert unrelated work.
- Keep PlatformIO build, upload, and filesystem upload flows non-interactive.
- Do not `--amend` or force-push on `main`.

## Firmware Hard Rules

- Do not call I2C, SPI, OLED, power-gating, or blocking hardware work from AsyncTCP callbacks.
- Do not call `WiFi.setSleep(false)`.
- New global firmware state belongs in `include/parameter.h`.
- New cross-task globals shared with `loop()` must be `volatile`.
- Do not add `printfAll` broadcasts without `wsBroadcastHeapOk()`.
- Do not accumulate `String` byte-by-byte from a known-length buffer.
- Keep the ADS1232 driver in polling mode; do not call `scale.beginTask()`.

## Code Style

- No comments in code; use clear names and small functions.
- No emojis in comments or documentation.
- Prefer immutability when practical.
- Prefer many small files over a few large files.
- Keep files around 200-400 lines when practical; 800 lines is the ceiling.
- Functions and locals are camelCase. Do not churn legacy snake_case.
- Timer deltas use `unsigned long` and `millis() - lastUpdate`.

## Deep References

- Fast file routing: `docs/AI_REPO_MAP.md`.
- Firmware footguns and troubleshooting: `docs/AI_FIRMWARE_NOTES.md`.
- Persistent settings, NVS namespaces, and EEPROM migration: `docs/AI_STORAGE_NOTES.md`.
- Decent binary, BLE, USB, and WebSocket compatibility: `docs/AI_PROTOCOL_NOTES.md`.
- GPIO, board pins, wake pins, and sleep holds: `docs/AI_GPIO_NOTES.md`.
- Build, flash, serial, and local tooling notes: `docs/AI_BUILD_NOTES.md`.
- OTA and release policy: `docs/AI_OTA_NOTES.md`.
