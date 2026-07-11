# AI Repo Map

Use this before broader docs when you need fast orientation. Read only the subsystem files that match the task, then widen to `README.md` if the local code is not enough.

This map is based on tracked project files. Local-only build, flash, OTA, editor, and cache directories are intentionally omitted.

## Subsystems

- Build and project config: `platformio.ini`, `check_platform_freshness.py`, `git_rev_macro.py`, `.github/workflows/nightly.yml`, `.github/workflows/release.yml`.
- Board, GPIO pins, and globals: `docs/AI_GPIO_NOTES.md`, `include/config.h`, `include/parameter.h`, `include/declare.h`.
- Main firmware loop, buttons, scale output, OLED: `src/hds.ino`, `include/menu.h`, `include/display.h`, `include/finger_detection.h`.
- Load-cell calibration and stability rules: `include/calibration_validation.h`, calibration paths in `include/menu.h`, ADC/tare paths in `src/hds.ino`.
- Persistent settings and migrations: `docs/AI_STORAGE_NOTES.md`, `include/storage.h`, `tools/test_storage_migration_contract.py`.
- Decent protocol shared helpers and compatibility rules: `docs/AI_PROTOCOL_NOTES.md`, `include/decent_protocol.h`.
- BLE transport: `include/ble.h`.
- USB transport and ADS debug packets: `include/usbcomm.h`, `tools/ads_debug_protocol.py`, `tools/decode_ads_debug.py`, `tools/ads_debug_monitor.py`, `tools/ads_debug_monitor_ble.py`.
- WiFi setup, AP/STA, mDNS, reconnect supervision: `src/wifi_setup.cpp`, `include/wifi_setup.h`.
- HTTP server and LittleFS routing: `include/webserver.h`, `web_apps/index.html`.
- `/snapshot` WebSocket protocol: `include/websocket.h`, WebSocket probes in `tools/ws_*.py`.
- Power, soft sleep, wake, shutdown: `include/power.h`, `include/websocket.h`, soft-sleep paths in `src/hds.ino`.
- WiFi OTA, signed manifests, staged LittleFS, rollback: `include/wifi_ota.h`, `include/pull_ota.h`, `include/ota_rollback.h`, `tools/generate_release_manifest.py`, `tools/write_ota_public_key_header.py`, `.github/workflows/release.yml`.
- Motion and ESP-NOW: `include/gyro.h`, `include/espnow.h`.
- On-device web apps: `web_apps/shared/`, `web_apps/Weigh_Save/`, `web_apps/dosing_assistant/`, `web_apps/Quality_Control_Assistant/`.
- Hardware and mechanical references: `Hardware/README.md`, `Scale Case/README.md`, STEP files in `Scale Case/`.

## Start Here

- Build flags, libraries, framework version, or filesystem image changes: `platformio.ini`, then the matching workflow in `.github/workflows/`.
- Pin, PCB, wake, sleep-hold, feature flag, or board variant changes: `docs/AI_GPIO_NOTES.md`, then `include/config.h`, `include/power.h`, and `include/parameter.h` for global state.
- New global firmware state: `include/parameter.h`.
- Main-loop behavior, button behavior, weighing output, or OLED drawing: `src/hds.ino`, then `include/menu.h`, `include/display.h`, or `include/finger_detection.h`.
- Calibration or ADC discontinuity fixes: `include/calibration_validation.h`, `include/menu.h`, and the tare/sample-window helpers in `src/hds.ino`.
- Persistent setting, default, key, schema, or EEPROM migration changes: `docs/AI_STORAGE_NOTES.md`, then `include/storage.h`.
- BLE or USB command behavior: `docs/AI_PROTOCOL_NOTES.md`, `include/decent_protocol.h`, then `include/ble.h` or `include/usbcomm.h`.
- ADS debug packet work: `include/usbcomm.h`, `tools/ads_debug_protocol.py`, `tools/decode_ads_debug.py`, and `tools/README_ADS_DEBUG.md`.
- WiFi connect, AP setup, credentials, mDNS, or reconnect bugs: `src/wifi_setup.cpp`, then `include/webserver.h` if HTTP startup is involved.
- HTTP route or static file serving changes: `include/webserver.h`, then the matching file under `web_apps/`.
- WebSocket command, event, status, or rate changes: `include/websocket.h`, then `tools/ws_feature_test.py` or `tools/ws_command_test.py`.
- Power-off, low-power, display sleep, or soft-sleep changes: `include/power.h`, `include/websocket.h`, and the top of `loop()` in `src/hds.ino`.
- OTA picker, manifest validation, signing, release assets, or staged LittleFS changes: `include/pull_ota.h`, `include/wifi_ota.h`, `include/ota_rollback.h`, `tools/generate_release_manifest.py`, and `.github/workflows/release.yml`.
- Web app UI or browser-side scale logic: start in that app's `main.js`, then its `modules/`, then `web_apps/shared/`.

## Focused Tests

- Firmware build: `pio run -e esp32s3`.
- Release manifest generation: `python tools/test_generate_release_manifest.py`.
- OTA contracts: `python tools/test_pull_ota_contract.py`, `python tools/test_release_workflow_contract.py`, `python tools/test_ota_rollback_contract.py`, `python tools/test_ota_public_key_header.py`.
- Calibration and soft-sleep contracts: `python tools/test_calibration_validation.py`, `python tools/test_soft_sleep_ads_wake.py`.
- NVS settings migration: `python tools/test_storage_migration_contract.py`.
- ADS debug protocol: `python tools/test_ads_debug_protocol.py`, `python tools/test_decode_ads_debug.py`.
- WebSocket device regression, with WiFi enabled on a scale: `python tools/ws_feature_test.py`.
- WebSocket command smoke test, with WiFi enabled on a scale: `python tools/ws_command_test.py`.

## Avoid Reading Unless Needed

- `.pio.nosync/`, `.flash-*`, `.ota-*`, `.vscode/`, and `tools/__pycache__/` are local/generated noise.
- `web_apps/` is noisy for firmware-only changes; open only the specific app or shared module that matches the task.
- `Scale Case/*.step` files are large CAD assets; skip them unless the task is mechanical.
- `assets/esp-tool.png` is only for flashing documentation.
- `README.md` is useful project context, but read task-local code first.
- Large protocol or menu headers are best entered through `rg` for the command, constant, or UI label before opening the whole file.
