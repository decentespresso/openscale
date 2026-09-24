# AI Repo Map

Use this map for unfamiliar or cross-subsystem work. For known files, start there and open only the matching topic note when needed. Do not preload all notes.

Local build, flash, OTA, editor, cache, and CAD files are omitted unless they are the task.

## Task Routing

| Work | Start with | Then read |
| --- | --- | --- |
| Build, dependencies, framework, filesystem image | `platformio.ini`, matching `.github/workflows/` file | `docs/AI_BUILD_NOTES.md` |
| Board pins, wake, sleep holds | `include/config.h`, `include/power.h`, `src/hds.ino` | `docs/AI_GPIO_NOTES.md` |
| Compile-time feature flags, build environments | `platformio.ini`, `include/config.h`, `src/hds.ino` | `docs/AI_BUILD_NOTES.md` |
| Approved plugins, patch packages, plugin CI | matching `plugins/<plugin-id>/plugin.json`, `tools/configure_custom_build.py`, `tools/build_custom_firmware.py` | `docs/AI_PLUGIN_NOTES.md`, `docs/plugin-development.md` |
| Global firmware state | `include/parameter.h`, then every reader and writer | `docs/AI_FIRMWARE_NOTES.md` |
| Main loop, buttons, weighing, OLED | `docs/AI_DISPLAY_NOTES.md`, `src/hds.ino`, `include/menu.h`, `include/display.h`, `include/finger_detection.h`, `include/tap_detection.h` | `docs/AI_FIRMWARE_NOTES.md` |
| Calibration, ADC, tare stability | `include/calibration_validation.h`, `include/menu.h`, `src/hds.ino` | `docs/AI_FIRMWARE_NOTES.md` |
| Persistent settings, defaults, migration | `include/storage.h` | `docs/AI_STORAGE_NOTES.md` |
| Decent binary, BLE, USB, ADS debug | `include/decent_protocol.h`, then `include/ble.h` or `include/usbcomm.h` | `docs/AI_PROTOCOL_NOTES.md` |
| WiFi, AP setup, credentials, mDNS | `src/wifi_setup.cpp`, `include/wifi_setup.h`, `include/mdns_name.h` | `docs/AI_BUILD_NOTES.md` |
| HTTP and LittleFS serving | `include/webserver.h`, matching `plugins/default-web-apps/assets/` file | `docs/AI_FIRMWARE_NOTES.md` |
| HDS web theme, CSS, and preference toggle | `plugins/default-web-apps/assets/shared/theme.css`, `plugins/default-web-apps/assets/shared/theme.js`, matching LittleFS page | `docs/AI_WEB_UI_NOTES.md` |
| `/snapshot` WebSocket behavior | `include/websocket.h` | `docs/AI_FIRMWARE_NOTES.md`, `docs/AI_PROTOCOL_NOTES.md` |
| Power, soft sleep, wake, shutdown | `include/power.h`, `include/websocket.h`, `include/wake_on_weight.h`, `src/hds.ino` | `docs/AI_GPIO_NOTES.md`, `docs/AI_FIRMWARE_NOTES.md` |
| Auto-off activity, optional Light Sleep, energy settings | `include/auto_off_activity.h`, `include/energy_policy.h`, `include/energy_menu.h`, `src/hds.ino` | `docs/AI_FIRMWARE_NOTES.md`, `docs/AI_STORAGE_NOTES.md`, `docs/AI_BUILD_NOTES.md` |
| WiFi OTA, manifests, signing, rollback | `include/pull_ota.h`, `include/ota_rollback.h`, `.github/workflows/release.yml` | `docs/AI_OTA_NOTES.md` |
| Release preparation, tagging, assets, and documentation audit | `.github/workflows/release.yml`, `tools/generate_release_manifest.py`, `README.md`, changes since the previous release tag | `docs/AI_RELEASE_NOTES.md`, plus affected topic notes |
| Motion and ESP-NOW | `include/gyro.h`, `include/espnow.h` | `docs/AI_GPIO_NOTES.md` when pins or power are involved |
| Web apps | matching file under `plugins/quality-control-assistant/assets/` or `plugins/default-web-apps/assets/`, then `plugins/default-web-apps/assets/shared/` | `docs/AI_WEB_UI_NOTES.md`, then `README.md` only when the local code is insufficient |
| Plugin webapps, previews, and handbooks | `tools/configure_custom_build.py`, `tools/plugin_presentation.py`, `docs/plugin-development.md` | `docs/custom-build/app.js`, `cloudflare/custom-build-worker/src/worker.mjs` |
| Hardware and mechanics | `Hardware/README.md`, `Scale Case/README.md` | matching STEP file only when required |

Run the focused checks listed in the matching `docs/AI_*_NOTES.md` file. Widen to `README.md` only when task-local code and notes are insufficient.

Validate this map and its source-derived facts with `python tools/test_ai_docs_contract.py`.
