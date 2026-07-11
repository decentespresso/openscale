# AI Build Notes

Read this when building, flashing, using serial tools, or touching PlatformIO configuration.

## Common Commands

Run commands from the repo root.

```sh
pio run -e esp32s3
pio run -e esp32s3 -t upload --upload-port <port>
pio run -e esp32s3 -t uploadfs --upload-port <port>
```

Firmware-only flashing does not update `web_apps/`. Flash LittleFS when the on-device web UI matters.

## PlatformIO Details

- `platformio.ini` uses `.pio.nosync` as the workspace directory.
- `web_apps/` is the LittleFS data directory.
- `git_rev_macro.py` injects `GIT_REV`; non-git source trees fall back to `nogit0`.
- `CONFIG_ASYNC_TCP_RUNNING_CORE=1` pins AsyncTCP to core 1.
- `ELEGANTOTA_USE_ASYNC_WEBSERVER=1` is set; `ElegantOTA.loop()` runs in `loop()`.
- `.gitattributes` enforces LF line endings repo-wide.

## Serial Capture

```sh
python tools/serial_tap.py <port> --baud 115200
```

## Device Discovery

The scale advertises `hds.local` and `_decentscale._tcp` with `path=/snapshot`, `proto=ws`, `model=hds`, and firmware metadata.

If no WiFi credentials are stored, the device starts AP setup mode:

- SSID: `DecentScale`
- password: `12345678`
- IP: `192.168.1.1`

## Focused Checks

Use the smallest relevant check:

```sh
pio run -e esp32s3
python tools/test_calibration_validation.py
python tools/test_soft_sleep_ads_wake.py
python tools/test_generate_release_manifest.py
python tools/test_pull_ota_contract.py
python tools/test_release_workflow_contract.py
python tools/test_ota_rollback_contract.py
python tools/test_ota_public_key_header.py
python tools/test_ads_debug_protocol.py
python tools/test_decode_ads_debug.py
```

Device-required checks:

```sh
python tools/ws_feature_test.py
python tools/ws_command_test.py
```
