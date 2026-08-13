# AI OTA Notes

Read this for WiFi OTA, signed manifests, release-asset policy, rollback, and
release-picker behavior. For end-to-end release preparation and the
AI-documentation audit, read `docs/AI_RELEASE_NOTES.md`.

## Policy

- Production WiFi pull OTA starts at `v3.1.13`.
- A signed `v3.1.12` manifest may exist for bootstrapping tests, but production picker code must not offer it.
- Signed manifests are mandatory. The device has no unsigned fallback.
- Never use `setInsecure`.
- Keep HTTPS CA validation, GitHub release asset URL allowlisting, exact size checks, and SHA-256 verification.
- Do not add a production option to skip `littlefs.bin`.
- Keep signing with Key 1 until firmware containing all three public keys has rolled out.
- Firmware predating the three-key migration cannot recover with Key 2 or Key 3 if the Key 1 private key is lost.
- Keep a lost private key's public key in firmware unless the key was compromised.

## Release Assets

Release builds publish WiFi OTA assets at the GitHub Release root:

- `firmware.bin`
- `littlefs.bin`
- `manifest.json`
- `manifest.sig`

The release workflow creates draft releases, uploads binary assets first, uploads `manifest.sig` and `manifest.json` last, then publishes the release.

Catalog manifests merge the previous latest stable signed manifest, dedupe by model, PCB, version, chip, and environment, sort newest to oldest, and keep the latest 10 production entries at `v3.1.13+`. Generated JSON is compact and limited to 16 KiB.

When the installed release has aged out of the latest catalog, firmware fetches that release's own signed manifest using both supported tag forms. It accepts only the compatible top-level release entry with the exact installed version and required LittleFS metadata. Firmware rollback remains local in the other app slot; this fallback supplies the signed asset metadata needed to restore the single shared LittleFS partition.

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

Staged LittleFS OTA stores target and rollback metadata in NVS namespace `ota_fs`, including `target_try`, `fs_dirty`, `restore`, and `restore_try` recovery state.

Flow:

1. Current firmware stores both manifests, writes `firmware.bin` to the inactive app slot, and reboots.
2. The new firmware calls `hdsOtaRollbackBegin()`, detects pending LittleFS work, and runs `pullOtaResumePendingLittleFs()` before application validity marking.
3. Recovery first accepts an already-written filesystem that passes size/schema, raw SHA-256, and mount checks. Otherwise, the target filesystem gets at most two attempts. Each attempt is recorded before network or write work; `fs_dirty` is recorded before `Update.begin()` can modify the partition.
4. A successful target write passes the same checks. The new application is then marked valid before pending state is cleared, and the device reboots.
5. If both attempts fail before filesystem writing begins, `setup()` calls `hdsOtaRollback("LittleFS update")`; the previous application remains paired with its unchanged filesystem.
6. If writing may have begun, recovery first replaces pending state with the previous application's matching signed filesystem asset, then rolls the application back. The previous application gets one recorded restore attempt.
7. A failed restore stops at `UPDATE ERROR`. A successful restore clears pending state and reboots.

Invalid or version-mismatched pending metadata is cleared only by the validation and terminal paths implemented in `pullOtaLoadPendingLittleFs()`. A boot with no pending filesystem transaction can call `hdsOtaRollbackMarkValid()` after normal setup validation.

The single LittleFS partition has no independent rollback slot. Do not weaken signed rollback-asset validation, bounded attempts, persisted write-state tracking, or the stop-on-failed-restore behavior.

`HDS_FEATURE_LITTLEFS` controls runtime filesystem use by the webserver and plugins. It does not disable or alter Pull OTA's mandatory staged `littlefs.bin` transaction.

## Rollback

`include/ota_rollback.h` overrides Arduino's weak `verifyRollbackLater()` so `ESP_OTA_IMG_PENDING_VERIFY` images are not auto-marked valid during `initArduino()`.

`setup()` calls `hdsOtaRollbackBegin()` after reset-reason capture. Without pending filesystem work it calls `hdsOtaRollbackMarkValid()` after setup validation. With pending work, validity is deferred until target recovery succeeds; recovery metadata is cleared only after application validity marking succeeds.

## Reboot Routing

ElegantOTA auto-reboot stays disabled with `ElegantOTA.setAutoReboot(false)`. Successful ElegantOTA and pull OTA paths call `remoteQueueOtaResetAt()` so their reset remains distinguishable from ordinary remote resets during an active update. Other scheduled main-loop resets use `remoteQueueResetAt()`; neither path should call `ESP.restart()` directly.

The rollback path withdraws WiFi and mDNS with `stopWifi()` before `esp_ota_mark_app_invalid_rollback_and_reboot()`. Do not bypass this routing: a direct reboot can leave the service advertised until resolver caches expire.

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
python tools/test_ota_reboot_routing_contract.py
pio run -e esp32s3
```
