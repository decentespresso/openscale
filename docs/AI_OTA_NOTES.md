# AI OTA Notes

Read this for WiFi OTA, signed manifests, release workflows, rollback, and release picker behavior.

## Policy

- Production WiFi pull OTA starts at `v3.1.13`.
- A signed `v3.1.12` manifest may exist for bootstrapping tests, but production picker code must not offer it.
- Signed manifests are mandatory. The device has no unsigned fallback.
- Never use `setInsecure`.
- Keep HTTPS CA validation, GitHub release asset URL allowlisting, exact size checks, and SHA-256 verification.
- Do not add a production option to skip `littlefs.bin`.

## Release Assets

Release builds publish WiFi OTA assets at the GitHub Release root:

- `firmware.bin`
- `littlefs.bin`
- `manifest.json`
- `manifest.sig`

The release workflow creates draft releases, uploads binary assets first, uploads `manifest.sig` and `manifest.json` last, then publishes the release.

Catalog manifests merge the previous latest stable signed manifest, dedupe by model, PCB, version, chip, and environment, sort newest to oldest, and keep production entries at `v3.1.13+`.

## Picker Rules

The on-device WiFi Update menu accepts stable numeric `vX.Y.Z` or `X.Y.Z` entries only.

Do not offer:

- preview releases
- RC releases
- draft releases
- malformed versions
- versions before `v3.1.13`
- incompatible hardware
- the same installed version

Compatible older stable releases at `v3.1.13+` may be offered as signed downgrades.

## Staged LittleFS OTA

Staged LittleFS OTA stores pending metadata in NVS namespace `ota_fs`.

Flow:

1. Current firmware downloads and writes `firmware.bin` to the inactive OTA app slot.
2. Device reboots into the new firmware.
3. New firmware marks the app valid after local validation.
4. New firmware downloads and writes `littlefs.bin` with `U_SPIFFS`.
5. New firmware verifies raw partition SHA-256, partition size/schema, and LittleFS mount.
6. New firmware clears pending state and reboots.

Pending LittleFS state survives failed WiFi, download, or write attempts so the next boot or WiFi Update menu can retry.

This avoids old firmware intentionally booting with a new filesystem. It is not the same as A/B data partitions; power loss during the LittleFS write can still leave the single filesystem partition incomplete, but the new firmware owns retry and recovery.

## Rollback

`include/ota_rollback.h` overrides Arduino's weak `verifyRollbackLater()` so `ESP_OTA_IMG_PENDING_VERIFY` images are not auto-marked valid during `initArduino()`.

`setup()` calls `hdsOtaRollbackBegin()` after reset-reason capture and `hdsOtaRollbackMarkValid()` after setup reaches local validation.

For staged LittleFS updates, the app is marked valid before writing the data partition, so the new firmware owns retry/recovery and old firmware is not reintroduced after a partial filesystem write.

## OTA Files And Tests

Core files:

- `include/wifi_ota.h`
- `include/pull_ota.h`
- `include/ota_rollback.h`
- `tools/generate_release_manifest.py`
- `tools/write_ota_public_key_header.py`
- `.github/workflows/release.yml`

Relevant checks:

```sh
python tools/test_generate_release_manifest.py
python tools/test_pull_ota_contract.py
python tools/test_release_workflow_contract.py
python tools/test_ota_rollback_contract.py
python tools/test_ota_public_key_header.py
pio run -e esp32s3
```
