# AI Build Notes

Read this when building, flashing, using serial tools, or touching PlatformIO configuration.

## Common Commands

Run commands from the repo root.

```sh
pio run -e esp32s3
pio run -e esp32s3 -t upload --upload-port <port>
pio run -e esp32s3 -t uploadfs --upload-port <port>
```

Firmware-only flashing does not update `plugins/default-web-apps/assets/`. Flash LittleFS when the on-device web UI matters.

## Environment Selection

Build only the environments affected by the change:

- Use `esp32s3` for changes limited to core firmware, weighing, or ADS communication, including an ADS1232 dependency-pin update.
- Add `esp32s3-grinder` only for grinder-specific code, feature flags, or build configuration.
- Add `esp32s3-custom` only for custom-build composition, patches, generated inputs, or shared feature-selection changes.

Do not add an environment merely because it exists. Changes limited to ADS1232 behavior or its dependency pin do not require the grinder or custom environments; those environments do not alter the weighing or ADS communication path.

## PlatformIO Details

- `platformio.ini` uses `.pio.nosync` as the workspace directory.
- PlatformIO Core is pinned by `requirements-platformio.txt`; install it with `python -m pip install --requirement requirements-platformio.txt` before running PlatformIO.
- `platformio.ini` pins the pioarduino platform URL and every `lib_deps` entry to an explicit registry version or git commit. Treat `platformio.ini` and `requirements-platformio.txt` as pinned dependency inputs, not optional setup files.
- CI records the PlatformIO version and `pio pkg list -e <environment>` in `dependencies.txt` for release and nightly dependency inventories.
- Every PlatformIO build validates the repository OTA public keys and regenerates `.pio.nosync/generated/include/ota_public_key.h`.
- Firmware builds require OpenSSL through `OPENSSL`, `PATH`, or Git for Windows with `git.exe` on `PATH`; clean targets do not.
- `plugins/default-web-apps/assets/` is the default LittleFS data directory.
- `gzip_web_assets.py` generates deterministic `.gz` siblings before LittleFS image builds.
- `git_rev_macro.py` requires a Git checkout and injects `GIT_REV`. Explicit `HDS_FIRMWARE_VERSION` overrides retain the release/custom identity; ordinary builds retain the version in `include/config.h`.
- `CONFIG_ASYNC_TCP_RUNNING_CORE=1` pins AsyncTCP to core 1, and `CONFIG_ASYNC_TCP_STACK_SIZE=8192` gives the AsyncTCP task an 8 KiB stack.
- `ELEGANTOTA_USE_ASYNC_WEBSERVER=1` is set; `ElegantOTA.loop()` runs in `loop()`.
- `include/hds_features.h` keeps the normal `esp32s3` feature defaults. `esp32s3-custom` reads `custom-build.json` through `tools/configure_custom_build.py` and stages generated headers and filesystem data under `.pio.nosync`.
- `tools/build_custom_firmware.py` resolves an allowed firmware ref to an exact commit, verifies the exact trusted builder commit, builds in a temporary checkout, applies plugin dependencies before dependents, and publishes firmware, LittleFS, dependency, and provenance artifacts only after the full build succeeds. It injects the trusted builder's custom configurator and version generator into the source checkout and rejects source revisions without the compatible `esp32s3-custom` hooks.
- `tools/build_custom_firmware.py --verify-plugin-environment esp32s3-<plugin-id>` applies one selected target plugin and its dependencies in an isolated checkout, runs its matching contract scripts, and builds its dedicated validation environment without publishing artifacts.
- Custom builds inject a version ending in `-custom`. A stable tag such as `v3.1.14` reports `3.1.14-custom`; `main` appends `-custom` to the version declared in `include/config.h`.
- Custom feature lists are exact. An empty list builds BLE/USB-only firmware; Pull OTA is enabled only when `pull-ota` is selected.
- WiFi-only custom builds include `webserver` and use the shared embedded setup page and verified setup endpoints without LittleFS.
- `HDS_FEATURE_LITTLEFS` controls runtime filesystem mounting and web assets. Pull OTA retains its staged `littlefs.bin` transaction independently of that runtime feature.
- `check_platform_freshness.py` compares the explicit pioarduino pin with the latest stable release as an advisory pre-build check. It never fails offline builds; set `HDS_SKIP_PLATFORM_CHECK=1` only when the check should be skipped deliberately.
- `.gitattributes` enforces LF line endings repo-wide.

### Windows Energy-Menu Builds

Before a fresh PM-capable build, constrain the nested pioarduino Core:

```powershell
$env:UV_CONSTRAINT = Join-Path $PWD 'constraints-pioarduino.txt'
```

On Linux/macOS use `export UV_CONSTRAINT="$PWD/constraints-pioarduino.txt"`.
The SDK builder installs its own Core into the selected core directory's
`penv`. Version 6.2.0 switches SCons to 4.11.1 during the nested Arduino build,
conflicting with this platform's SCons 4.8.1. The constraint keeps that Core
at 6.1.19 without changing the firmware or disabling size checks. CI applies
it to Energy Menu and custom builds. Constraints do not downgrade an already
installed Core; use a fresh ignored local core directory in that case.

The PM-capable energy-menu environments compile ESP-IDF libraries. Use a
short physical worktree path, such as `D:\w180`, with its ignored local
`.pio-core` directory. Set `PLATFORMIO_BUILD_DIR` to `D:\w180\build` for
that build. Long paths can exceed Windows command-line limits during
linker-script generation. A `subst` alias is not sufficient: CMake resolves
the physical path while other build steps keep the alias, which can break
generated certificate source paths. Do not change the firmware or dependency
pins to work around these path errors.

Keep normal and custom-SDK builds in separate core directories. The custom
builder rewrites the installed framework; switching variants in one core
can leave mismatched headers and libraries. For example, use `.pio-core` for
the energy-menu build and `build\normal-core` for the normal build, setting
`PLATFORMIO_CORE_DIR` per invocation. Both directories are ignored.

## Enabling a Stable Custom Base

The builder derives tagged versions from the tag rather than the source header. `v3.1.14` produces `3.1.14-custom`; `v3.1.14-preview.3` produces `3.1.14-preview.3-custom`. Custom preview builds have their own signed manifests and combination hashes. The non-custom preview.3 release has no signed rollback manifest; official preview.4 has signed firmware/LittleFS recovery assets.

The configurator offers `3.1.14 (stable)` by default and `main (development)` for testing. `selectableFirmwareRefs()` in `docs/custom-build/selection.mjs` excludes previews from the selector and URL selections. Main build requests, including retries, require confirmation. Backend catalogs and the Worker allow-list retain preview compatibility; hiding a source in the browser does not delete saved builds or recovery assets. Main builds still consume the shared build quota.

Stable source support requires matching `FIRMWARE_REFS`, plugin manifests, generated catalogs, and Worker `ALLOWED_FIRMWARE_REFS`. Deploy the stable-default configurator only after the real tag exists, the trusted builder can build it, and Worker support is deployed. Before tagging, CI may use a temporary local tag in its disposable checkout and must pass the resolved commit explicitly when compiling. See `docs/custom-build/operations.md` for service deployment and `docs/AI_RELEASE_NOTES.md` for release authorization.

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

`POST /setup/wifi` queues a direct connection test and returns HTTP 202 with `operation_id` and `restarting:false`; it no longer restarts the scale. `GET /setup/wifi/status` reports the matching result without a password. `POST /setup/wifi/scan` starts a bounded asynchronous scan; `GET /setup/wifi/scan` returns up to twenty deduplicated networks. Both web pages load the firmware-served `/setup/wifi.js` and `/setup/wifi.css`, including WiFi-only builds without LittleFS. Run `pio test -e native` and `python tools/test_wifi_setup_contract.py` when changing this flow.

The dashboard plugin also supports older firmware such as `v3.1.14`, which has no firmware-served WiFi script or scan/status endpoints. Its static `shared/wifi-legacy.js` and CSS fallback retain manual Connect, password visibility, and reset confirmation. The legacy endpoint returns an empty HTTP 200 and restarts after saving; the dashboard does not claim a verified connection. The firmware script marks the form to prevent the fallback from adding duplicate handlers on newer firmware. An OLED reset cancels a running connection test only after credential clearing succeeds; a failed reset leaves the test intact. Run `python tools/test_wifi_setup_runtime.py` and `node tools/test_wifi_setup_ui.cjs` for these paths.

WiFi-only custom builds retain `webserver` to use the same verified setup endpoints without LittleFS. The previous socket-based setup server is removed.

Current setup pages pin the device identity from the first status response before enabling mutation controls. WiFi changes, resets, scans, and renames send `X-HDS-Device-ID`; firmware rejects mismatched identities before changing state. The header remains optional for existing direct API clients. Status, accepted replies, and scan results must retain the pinned identity. Candidate and recovery connections observed at or after the 20-second deadline cannot override the timeout. Disconnects or renewed DHCP generations invalidate stability checks, and failed credential resets retain recovery AP retry timing.

After an accepted WiFi switch or reset, both current setup pages ask the user to confirm their phone/computer's network in a styled dialog. Continue resumes operation-specific status checks on the current page; Check later or Escape does not cancel the firmware operation. Contact loss never triggers navigation. Only a successful result from the matching operation and hardware-derived `device_id` exposes an explicit link to the reported IPv4 address. The link uses `/setup/wifi/continue` to verify the device before redirecting to `/`, retaining the dashboard when LittleFS is available and the embedded page otherwise. Failed, stale, wrong-device, and unconfirmed results expose no link. An unreachable changed IP must be read from the scale; without contact with the original address the browser cannot confirm the result. Legacy stable firmware keeps its separate save-and-restart flow.
