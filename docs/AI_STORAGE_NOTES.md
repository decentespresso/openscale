# AI Storage Notes

Read this when changing persistent settings, defaults, migrations, NVS namespaces, LittleFS update state, or browser storage. Skip it for changes that only use runtime globals.

## Source Of Truth

`include/storage.h` is the firmware settings source of truth and `tools/test_storage_migration_contract.py` is its contract check.

Do not reconstruct the active settings layout from the old address declarations in `include/parameter.h`. Those declarations are replaced by named keys in the `hds` NVS namespace.

## Storage Ownership

| Store | Owner | Purpose |
| --- | --- | --- |
| NVS `hds` | `include/storage.h` | Scale settings and their schema version. |
| NVS `wifi` | `src/wifi_settings.cpp` | WiFi SSID and password, plus the `mdns_name` device name (default `hds`). |
| NVS `ota_fs` | `include/pull_ota.h` | Pending staged LittleFS metadata. |
| NVS `ota_verify` | `include/ota_rollback.h` | OTA boot-verification attempt count. |
| NVS `ota_recovery` | `include/filesystem_recovery.h` | Persisted filesystem-free recovery state. |
| NVS `ota_custom` | `include/custom_build_ota.h` | Random device ID and device authentication secret. |
| LittleFS | `include/webserver.h`, OTA code | On-device web application files. |
| Browser `localStorage` | Files under `plugins/default-web-apps/assets/` | Per-browser application data; unrelated to device NVS. |

Keep these namespaces independent. A settings reset or migration must not clear WiFi credentials or OTA recovery state unless that behavior is explicitly requested.

The device name lives in `wifi`, not `hds`, so renaming a scale never touches the settings schema or its migration. Reset WiFi posts `{"ssid":""}` and clears credentials only. Name values are normalized and validated by `include/mdns_name.h`; invalid stored names fall back to the default at boot.

New WiFi credentials use the `credentials` blob: version byte `1`, a 33-byte terminated SSID buffer, and a 65-byte terminated password buffer. `WiFiParams` reads the old `ssid` and `pass` keys only when the blob is absent. It does not trim existing stored values. A network-change candidate stays in RAM until the worker observes a fresh DHCP address and three seconds of stability. The worker writes and verifies the complete blob before updating its active credentials. Reset writes an empty credential record and verifies removal of both legacy keys; incomplete clearing reports failure and restores the previous credential blob. It never clears the namespace. Downgrading to firmware that only knows the legacy keys does not recover a newly saved network.

Manual WiFi input trims Unicode whitespace and invisible characters at both edges. Selecting a scan result preserves the byte-exact SSID and password, including meaningful edge spaces. The firmware permits this only when the submitted SSID exactly matches its latest completed scan; UTF-8, control-character, and byte-length validation still applies. A failed scan clears the browser's old choices. Recovery AP retries the saved network after ten minutes, with a 20-second deadline before reopening the AP if that retry fails.

## HDS Schema Version 1

`include/storage.h` defines the exact names, types, and defaults. The current schema is:

| Key | Type | Default |
| --- | --- | --- |
| `cal1` | float | `CALIBRATION_VALUE_DEFAULT` |
| `container` | float | `0.0` |
| `mode` | int | `0` |
| `pourover` | float | `16.0` |
| `espresso` | float | `18.0` |
| `beep` | int | `1` |
| `welcome` | string | `WELCOME1` |
| `bat_cal` | float | `1.66` on V7.2, otherwise `1.06` |
| `heartbeat` | bool | `true` |
| `screen_flip` | bool | `false` |
| `time_on_top` | bool | `false` |
| `btn_conn` | bool | `false` |
| `wifi_boot` | bool | `false` |
| `auto_sleep` | bool | `true` |
| `quick_boot` | bool | `false` |
| `drift_max` | float | `0.05` |
| `tap_tare` | bool | `false` |
| `tap_timer` | bool | `false` |
| `wow_interval` | int | `0` (off); indices 1-3 select 2-4 second sleep intervals. |

`schema` is a `uint16_t`, currently `1`. Do not rename a key or change its stored type in place. Add an explicit migration, preserve old data until the new schema is complete, then increment the schema version.

Energy-menu settings use `energy_schema` and `ENERGY_FEATURE_KEYS` in `include/storage.h`, independently of the base settings schema. Missing or invalid booleans receive validated defaults.

## Legacy EEPROM Migration

Migration is one-way for new firmware but deliberately non-destructive for rollback:

1. Open the `hds` namespace read/write.
2. If the stored schema is current, ensure missing keys receive defaults and do not open EEPROM.
3. If the schema is missing and not all keys exist, open the legacy 512-byte EEPROM blob and read the old sequential layout.
4. Write only missing NVS keys. A retry must not overwrite keys already migrated by an earlier partial attempt.
5. Confirm every required key exists before writing the schema marker.
6. Close EEPROM without clearing or rewriting it.

The retained EEPROM blob lets pre-NVS firmware recover its old settings after a rollback. Do not erase it as migration cleanup.

The legacy layout depends on `float == 4`, `int == 4`, and `bool == 1`, enforced by `static_assert`. The welcome slot is 128 bytes and begins with `HDSW`. Calibration, mode, buzzer, boolean, finite-number, and board-specific battery values are validated before migration; erased or invalid values fall back to defaults.

If NVS initialization, migration, or the final schema write fails, `storageInit()` fails and setup stops rather than booting with partially trusted settings.

## Browser Storage

Browser `localStorage` is scoped to the browser origin, not to an individual app directory. The dosing preset manager uses `decentScaleDosingPresets` and `lastUsedDosingPreset`; Quality Control Assistant uses `decentScaleQCPresets` and `lastUsedQCPreset`. Each preset store contains `{version: 1, presets: {...}}`. The apps ignore unsupported versions and refuse to overwrite them.

The old shared keys, `decentScalePresets` and `lastUsedPreset`, contain incompatible preset shapes. Each app reads compatible legacy presets and selections only while its own preset store is absent. Its first save copies those presets and any compatible last-used selection without changing the shared data. An existing app-specific store ends both legacy fallbacks, even when its last-used selection is missing.

New browser-persisted data should use an app-specific key and a versioned object when its shape can evolve. Verify keys in implementation code: a README claim alone is not a storage contract.

## Checks

Run the smallest relevant checks:

```sh
python tools/test_storage_migration_contract.py
python tools/test_preset_isolation.py
pio run -e esp32s3
```

For a release-bound migration, also exercise a device upgrade with populated EEPROM, an interrupted migration retry, a clean-device boot, and rollback to pre-NVS firmware.
