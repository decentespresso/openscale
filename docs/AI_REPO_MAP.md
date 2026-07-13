# AI Repo Map

Use this map for unfamiliar or cross-subsystem work. For known files, start there and open only the matching topic note when needed. Do not preload all notes.

Local build, flash, OTA, editor, cache, and CAD files are omitted unless they are the task.

## Task Routing

| Work | Start with | Then read |
| --- | --- | --- |
| Build, dependencies, framework, filesystem image | `platformio.ini`, matching `.github/workflows/` file | `docs/AI_BUILD_NOTES.md` |
| Board pins, wake, sleep holds, feature flags | `include/config.h`, `include/power.h`, `src/hds.ino` | `docs/AI_GPIO_NOTES.md` |
| Global firmware state | `include/parameter.h`, then every reader and writer | `docs/AI_FIRMWARE_NOTES.md` |
| Main loop, buttons, weighing, OLED | `src/hds.ino`, `include/menu.h`, `include/display.h`, `include/finger_detection.h` | `docs/AI_FIRMWARE_NOTES.md` |
| Calibration, ADC, tare stability | `include/calibration_validation.h`, `include/menu.h`, `src/hds.ino` | `docs/AI_FIRMWARE_NOTES.md` |
| Persistent settings, defaults, migration | `include/storage.h` | `docs/AI_STORAGE_NOTES.md` |
| Decent binary, BLE, USB, ADS debug | `include/decent_protocol.h`, then `include/ble.h` or `include/usbcomm.h` | `docs/AI_PROTOCOL_NOTES.md` |
| WiFi, AP setup, credentials, mDNS | `src/wifi_setup.cpp`, `include/wifi_setup.h` | `docs/AI_BUILD_NOTES.md` |
| HTTP and LittleFS serving | `include/webserver.h`, matching `web_apps/` file | `docs/AI_FIRMWARE_NOTES.md` |
| `/snapshot` WebSocket behavior | `include/websocket.h` | `docs/AI_FIRMWARE_NOTES.md`, `docs/AI_PROTOCOL_NOTES.md` |
| Power, soft sleep, wake, shutdown | `include/power.h`, `include/websocket.h`, `src/hds.ino` | `docs/AI_GPIO_NOTES.md`, `docs/AI_FIRMWARE_NOTES.md` |
| WiFi OTA, manifests, signing, rollback | `include/pull_ota.h`, `include/ota_rollback.h`, `.github/workflows/release.yml` | `docs/AI_OTA_NOTES.md` |
| Motion and ESP-NOW | `include/gyro.h`, `include/espnow.h` | `docs/AI_GPIO_NOTES.md` when pins or power are involved |
| Web apps | matching app `main.js`, its `modules/`, then `web_apps/shared/` | `README.md` only when the local code is insufficient |
| Hardware and mechanics | `Hardware/README.md`, `Scale Case/README.md` | matching STEP file only when required |

Run the focused checks listed in the matching `docs/AI_*_NOTES.md` file. Widen to `README.md` only when task-local code and notes are insufficient.
