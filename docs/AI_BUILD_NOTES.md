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
- PlatformIO Core is pinned by `requirements-platformio.txt`; install it with `python -m pip install --requirement requirements-platformio.txt` before running PlatformIO.
- `platformio.ini` pins the pioarduino platform URL and every `lib_deps` entry to an explicit registry version or git commit. Treat `platformio.ini` and `requirements-platformio.txt` as pinned dependency inputs, not optional setup files.
- CI records the PlatformIO version and `pio pkg list -e <environment>` in `dependencies.txt` for release and nightly dependency inventories.
- Every PlatformIO build validates the repository OTA public keys and regenerates `.pio.nosync/generated/include/ota_public_key.h`.
- Firmware builds require OpenSSL through `OPENSSL`, `PATH`, or Git for Windows with `git.exe` on `PATH`; clean targets do not.
- `web_apps/` is the LittleFS data directory.
- `gzip_web_assets.py` generates deterministic `.gz` siblings before LittleFS image builds.
- `git_rev_macro.py` injects `GIT_REV`; non-git source trees fall back to `nogit0`.
- `CONFIG_ASYNC_TCP_RUNNING_CORE=1` pins AsyncTCP to core 1, and `CONFIG_ASYNC_TCP_STACK_SIZE=8192` gives the AsyncTCP task an 8 KiB stack.
- `ELEGANTOTA_USE_ASYNC_WEBSERVER=1` is set; `ElegantOTA.loop()` runs in `loop()`.
- `include/hds_features.h` keeps the normal `esp32s3` feature defaults. `esp32s3-custom` reads `custom-build.json` through `tools/configure_custom_build.py` and stages generated headers and filesystem data under `.pio.nosync`.
- `tools/build_custom_firmware.py` resolves an allowed firmware ref to an exact commit, builds it in a temporary checkout, applies approved patches in plugin-ID order, and publishes firmware, LittleFS, dependency, and provenance artifacts only after the full build succeeds.
- `tools/build_custom_firmware.py --verify-plugin-environment esp32s3-<plugin-id>` applies one patch plugin in an isolated checkout, runs its matching contract scripts, and builds its dedicated validation environment without publishing artifacts.
- Custom feature lists are exact. An empty list builds BLE/USB-only firmware; Pull OTA is enabled only when `pull-ota` is selected.
- `HDS_FEATURE_LITTLEFS` controls runtime filesystem mounting and web assets. Pull OTA retains its staged `littlefs.bin` transaction independently of that runtime feature.
- `check_platform_freshness.py` compares the explicit pioarduino pin with the latest stable release as an advisory pre-build check. It never fails offline builds; set `HDS_SKIP_PLATFORM_CHECK=1` only when the check should be skipped deliberately.
- `.gitattributes` enforces LF line endings repo-wide.

## Focused Checks

Run the checks that cover the build inputs and generated filesystem assets:

```sh
python tools/test_ai_docs_contract.py
python tools/test_mdns_name_contract.py
python tools/test_gzip_web_assets.py
python tools/test_plugin_catalog.py
python tools/test_custom_build_execution.py
python tools/test_plugin_ci_contract.py
python tools/test_release_workflow_contract.py
pio run -e esp32s3
pio run -e esp32s3 -t buildfs
```

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
