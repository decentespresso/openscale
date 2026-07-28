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
- Firmware builds require OpenSSL through `OPENSSL`, `PATH`, or Git for Windows with `git.exe` on `PATH`; clean targets do not.
- `web_apps/` is the LittleFS data directory.
- `gzip_web_assets.py` generates deterministic `.gz` siblings before LittleFS image builds.
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

The scale advertises `<name>.local` and `_decentscale._tcp` with `path=/snapshot`, `proto=ws`, `model=hds`, `name=<name>`, and firmware metadata. `<name>` is the stored device name from the NVS `wifi` namespace, default `hds`, validated by `include/mdns_name.h` and settable through `POST /setup/name`. The DNS-SD instance name is `Half Decent Scale` at the default and `Half Decent Scale (<name>)` otherwise. A rename only takes effect after the restart the endpoint queues, because `WiFi.setHostname()` is read before association.

`stopWifi()` withdraws the registration through `MDNS.end()` before the radio goes down. That call is what emits the DNS-SD goodbye, and every deliberate teardown -- remote or USB reset, rename and wifi-setup reboots, deep sleep -- routes through it. Skipping it leaves the instance in resolver caches for the PTR TTL (75 min by convention against 2 min for SRV/A), which browses as a service that never resolves. Withdrawal is best effort: the goodbye is one unacknowledged multicast, and crashes, flat batteries, and unplugs send nothing.

If no WiFi credentials are stored, `setupAP()` in `src/wifi_setup.cpp` starts provisioning mode. `README.md` contains the user-facing connection details.
