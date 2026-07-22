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
- Every PlatformIO build validates the repository OTA public keys and regenerates `.pio.nosync/generated/include/ota_public_key.h`.
- `web_apps/` is the LittleFS data directory.
- `git_rev_macro.py` injects `GIT_REV`; non-git source trees fall back to `nogit0`.
- `CONFIG_ASYNC_TCP_RUNNING_CORE=1` pins AsyncTCP to core 1.
- `ELEGANTOTA_USE_ASYNC_WEBSERVER=1` is set; `ElegantOTA.loop()` runs in `loop()`.
- `.gitattributes` enforces LF line endings repo-wide.

## Serial Capture

```sh
python tools/serial_tap.py <port> --baud 115200
```

`serial_tap.py` uses POSIX `termios` and is not available on Windows.

If PlatformIO monitoring is unavailable, pySerial provides a fallback:

```sh
python -m serial.tools.miniterm <port> 115200
```

Opening a serial terminal may toggle DTR or RTS and reset the device.

## Device Discovery

The scale advertises `hds.local` and `_decentscale._tcp` with `path=/snapshot`, `proto=ws`, `model=hds`, and firmware metadata.

If no WiFi credentials are stored, `setupAP()` in `src/wifi_setup.cpp` starts provisioning mode. `README.md` contains the user-facing connection details.
