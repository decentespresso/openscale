# Agent Instructions

For a narrow task with known files, start there and open only the matching `docs/AI_*_NOTES.md` file when needed. For unfamiliar or cross-subsystem work, use `docs/AI_REPO_MAP.md` for orientation. Do not preload all notes.

## Always

- For complex or risky work, make a brief plan before editing. Ask the user only when unresolved ambiguity materially affects the result.
- Security first; performance second.
- Preserve existing user changes. Do not revert unrelated work.
- Keep PlatformIO build, upload, and filesystem upload flows non-interactive.
- Do not `--amend` or force-push on `main`.

## Firmware Hard Rules

- Do not call I2C, SPI, OLED, power-gating, or blocking hardware work from AsyncTCP callbacks.
- Do not call `WiFi.setSleep(false)`.
- New global firmware state belongs in `include/parameter.h`.
- For cross-task state, follow the subsystem's synchronization pattern; `volatile` alone is not synchronization.
- Do not add `printfAll` broadcasts without `wsBroadcastHeapOk()`.
- Do not accumulate `String` byte-by-byte from a known-length buffer.
- Keep the ADS1232 driver in polling mode; do not call `scale.beginTask()`.

## Code Style

- Avoid comments that restate the code. Keep concise comments for non-obvious hardware, protocol, concurrency, safety, and compatibility constraints.
- Put long debugging history and cross-file rationale in the matching `docs/AI_*_NOTES.md` file.
- No emojis in comments or documentation.
- Prefer immutability when practical.
- Within the unity-style firmware architecture, prefer new handwritten implementation files around 200-400 lines and justify files over 800 lines. Do not split legacy files solely to meet this target.
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
