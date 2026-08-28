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

## Unattended Install

A start request may name a target release. Over BLE and USB that is the five-byte `0x1B` form; over the `/snapshot` WebSocket it is the `wifi_update` command's version argument. See `docs/AI_PROTOCOL_NOTES.md` for the wire forms. A request with no target keeps the interactive picker on every transport.

`pullOtaRunUpdate()` resolves a target against `selection`, the picker's own list, never against the raw catalog. That inheritance is the safeguard: `pullOtaAddParsedRelease()` has already dropped ineligible and incompatible entries, and `pullOtaBuildSelectableReleases()` has already dropped the installed version. A release the picker would not have offered is simply not found. Do not resolve against `catalog` or reimplement the eligibility rules; a second copy is where the two paths would drift.

The unattended path skips only `pullOtaPickRelease()` and `pullOtaConfirmInstall()`. Everything else is shared and unchanged: WiFi, clock, signed catalog fetch and signature verification, rollback-manifest resolution before any write, `pullOtaInstall()`, HTTPS with CA validation, asset URL allowlisting, exact size checks, SHA-256 verification, and the staged LittleFS transaction with its bounded retries and rollback metadata.

A request is refused when the target is absent from the verified catalog, incompatible with this hardware, not a stable numeric release, below `HDS_OTA_MIN_INSTALL_VERSION`, equal to the installed version, or when no signed rollback manifest can be resolved. A refusal calls `pullOtaFail()` and returns. It must never fall back to the picker, and it must never install a release other than the one requested.

An eligible downgrade is accepted, matching picker behavior.

A pending staged LittleFS transaction takes priority: `pullOtaRunUpdate()` resumes it and ignores the target.

The client contributes a version number and no other input. Asset URLs, sizes, and hashes all come from the scale's own signature-verified fetch.

Acceptance is not installability. A transport acknowledges only that a well-formed request was queued. Accept-time refusals are limited to pull OTA being compiled out, a malformed version, and an update already running. Every catalog-level refusal happens later on the `Pull OTA` task, after `pullOtaPauseFilesystemServices()` has called `stopWebServer()` and closed every WebSocket client, so those refusals surface on the display and the serial log only. Do not add a client progress stream; progress stays on the OLED.

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

ElegantOTA start and main-loop remote action dispatch share `otaDispatchMutex`. Hold it across the complete extracted action batch so `onOTAStart()` cannot publish active OTA until in-flight hardware work finishes. If OTA starts first, restore extracted actions without overwriting newer pending members of the display, low-power, soft-sleep, or timer replacement groups.

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
python tools/test_decent_ota_target_contract.py
python tools/test_pull_ota_contract.py
python tools/test_release_workflow_contract.py
python tools/test_ota_rollback_contract.py
python tools/test_ota_public_key_header.py
python tools/test_ota_reboot_routing_contract.py
pio run -e esp32s3
```
