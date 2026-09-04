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
- Custom OTA trusts two separate public-key slots. Key 1 signs through
  `HDS_CUSTOM_OTA_SIGNING_KEY_PEM`; Key 2 is the offline rotation reserve.
- Custom OTA assignments contain only a canonical combination hash, never an asset URL.

## Custom Fleet OTA

Opening the Custom Build menu performs one authenticated device check-in. Custom firmware reports
the normalized compile-time `HDS_CUSTOM_BUILD_COMBINATION_HASH`; official firmware reports null.
The response contains only linking state, one desired combination, and its canonical build state.

Validate the full desired hash before comparing it with the local compile-time hash. An exact match
must return before custom manifest or signature retrieval, asset download, pending LittleFS state,
or reboot scheduling. The Worker copy of `installed_combination` is telemetry and must never drive
this decision.

Fleet build references, scale lists, bulk assignment, and deployment history stay in the
Worker/browser. Firmware must not fetch a fleet build list, add fleet-sized collections, persist a
short hash, add background check-ins, or increase `HDS_OTA_TASK_STACK_BYTES` without measurement.
OLED identity uses an uppercase 8-character prefix for display only.

## Release Assets

Release builds publish WiFi OTA assets at the GitHub Release root:

- `firmware.bin`
- `littlefs.bin`
- `manifest.json`
- `manifest.sig`

The release workflow creates draft releases, uploads binary assets first, uploads `manifest.sig` and `manifest.json` last, then publishes the release.

Official releases and custom builds embed both committed custom OTA public keys. Custom builds publish
`firmware.bin`, `littlefs.bin`, `ota-manifest.sig`, and `ota-manifest.json` under the immutable
R2 combination-hash prefix. The custom manifest is accepted only when its separate signature,
combination hash, fixed Worker asset prefix, hardware compatibility, sizes, and SHA-256 hashes all
validate.

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

Only one start request exists at a time. Every transport admits through `remoteQueueWifiUpdate()`, USB included; nothing else may call `wifiUpdate()`. `pendingOtaDispatching` covers the gap between clearing the pending bit and the update being marked running, so a request cannot be accepted mid-dispatch. A request that reaches dispatch while an update is already running is dropped, not requeued: requeuing is what let a stale request fire once an unrelated update ended.

Acceptance is not installability. A transport acknowledges only that a well-formed request was queued. Accept-time refusals are limited to pull OTA being compiled out, a malformed version, and an update already running. Every catalog-level refusal happens later on the `Pull OTA` task, after `pullOtaPauseFilesystemServices()` has called `stopWebServer()` and closed every WebSocket client, so those refusals surface on the display and the serial log only. Do not add a client progress stream; progress stays on the OLED.

## Staged LittleFS OTA

Staged LittleFS OTA stores target and rollback metadata in NVS namespace `ota_fs`, including custom combination hashes and `target_try`, `fs_dirty`, `restore`, and `restore_try` recovery state. Custom-to-custom rollback fetches the installed combination's signed custom manifest and identifies both app slots by combination hash rather than their shared version string.

Flow:

1. Current firmware stores both manifests, writes `firmware.bin` to the inactive app slot, and reboots.
2. The new firmware calls `hdsOtaRollbackBegin()`, detects pending LittleFS work, leaves BLE uninitialized, and runs `pullOtaResumePendingLittleFs()` before application validity marking.
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

The project-owned `/ota/start` handler and main-loop remote action dispatch share `otaDispatchMutex`. The handler claims `b_ota`, waits for the main loop to stop BLE traffic, close WebSockets, wake the scale, and lock out energy-saving transitions, then calls `Update.begin()` under the mutex. It installs the requested MD5 only after `Update.begin()` because the ESP32 core clears the expected digest when an update begins. Pull OTA waits for the same main-loop pause before downloading or writing. Hold the mutex across the complete extracted action batch. Restore extracted actions through `remoteRestoreDeferredPendingLocked()` so older display, low-power, soft-sleep, or timer commands cannot replace newer pending commands.

Both OTA paths stop BLE advertising and wait for an active peer's disconnect callback before flash work. BLE writes are ignored after OTA claims the runtime. Disconnect callbacks defer the saved buzzer setting until a failed or cancelled update resumes advertising from the main loop; display state is not restored over newer deferred commands. WebSocket connect and data callbacks close without doing application work once OTA is active. Do not shut BLE down from an AsyncTCP callback.

While OTA is active, the main loop closes WebSocket clients and the HTTP middleware rejects new non-OTA work. The project-owned `/ota/upload` handler is registered before ElegantOTA's handler and checks Pull OTA ownership under `otaDispatchMutex` before every `Update.write()` and `Update.end()`. Uploads cannot claim the session until the main-loop pause is complete and `Update.begin()` is live. The first multipart file request then claims the ElegantOTA session, competing requests are rejected without touching that session, and only the owner can report success after its multipart callback completes `Update.end(true)`. The middleware alone is not an upload guard because ESPAsyncWebServer parses multipart bodies before running server middleware.

ElegantOTA accepts firmware images only. Direct filesystem uploads are rejected because they bypass the signed, staged LittleFS transaction and its rollback protection. Use the WiFi Update menu for firmware and filesystem release updates.

ElegantOTA firmware sessions with no progress for 30 seconds restart through the main-loop reset queue. This recovers both `Update.begin()` failures and uploads abandoned by a disconnected client without resuming normal work around an incomplete flash session.

ElegantOTA progress logging and full-screen OLED updates are limited to 2 Hz while the upload is active.

The rollback path withdraws WiFi and mDNS with `stopWifi()` before `esp_ota_mark_app_invalid_rollback_and_reboot()`. Do not bypass this routing: a direct reboot can leave the service advertised until resolver caches expire.

## OTA Files And Tests

Core files:

- `include/wifi_ota.h`
- `include/pull_ota.h`
- `include/ota_rollback.h`
- `include/custom_build_ota.h`
- `tools/generate_release_manifest.py`
- `tools/write_ota_public_key_header.py`
- `tools/write_custom_ota_public_key_header.py`
- `tools/generate_custom_ota_manifest.py`
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
python tools/test_custom_build_ota_contract.py
node --test cloudflare/custom-build-worker/test/fleet.test.mjs
node --test cloudflare/custom-build-worker/test/configurator.test.mjs
pio run -e esp32s3
```
