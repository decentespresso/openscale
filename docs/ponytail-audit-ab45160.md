# Ponytail Audit: ab45160

## Scope

Audited commit: `ab4516067c7bdb7f3d19816556793f7bea1e3422`.

This report records a sequential, read-only audit for over-engineering. It proposes deletions and simplifications; it applies none. Findings refer to the pinned commit, not future revisions.

The audit assigned all 254 tracked files to a subsystem or inventory-only exclusion. Coverage was evidence-backed, not line-by-line exhaustive. The audit traced callers, state ownership, build variants, plugin patches, interfaces, and relevant tests. CodeGraph supported navigation; exact tracked-source searches confirmed candidate references.

Recommendations preserve public APIs, wire formats, settings, artifact formats, calibration, synchronization, hardware timing, security checks, and recovery behavior.

## Coverage

- [x] 1. Build and dependency foundation
- [x] 2. Firmware orchestration
- [x] 3. Weighing and calibration
- [x] 4. Persistent settings
- [x] 5. Local interaction and display
- [x] 6. Power and energy
- [x] 7. Motion and peer communication
- [x] 8. Decent protocol and transports
- [x] 9. Network lifecycle and provisioning
- [x] 10. HTTP, WebSocket, and filesystem services
- [x] 11. Grinder integration
- [x] 12. Firmware updates
- [x] 13. Shared utilities, Weigh Save, Dosing Assistant, and Quality Control Assistant
- [x] 14. Plugin and custom-build pipeline, including Pressensor patches
- [x] 15. Hosted configurator and fleet UI
- [x] 16. Cloudflare service
- [x] 17. Release, CI, and developer tooling
- [x] 18. Tests, documentation, and cross-subsystem reconciliation

Inventory-only binary assets and CAD files:

- `Scale Case/half_decent_scale v9.step`
- `Scale Case/half_decent_scale_electrionics v8.step`
- `assets/esp-tool.png`
- `docs/custom-build/scale-workbench.webp`

Public trust keys remain unchanged. A reviewed subsystem without a ranked finding has no accepted reduction from this pass.

## Ranked Findings

Savings are estimates of net source-line reductions, counted once. Dependency savings mean removable direct declarations, not measured firmware-size reductions. These are implementation candidates, not tested patches.

1. delete: Weigh Save's unused dosing controls, progress/guidance methods, getters, and associated fields; replace with nothing; its callers never use these methods and its HTML lacks the dosing controls; estimated savings: 194 lines; validating check: webapp contracts plus timer, recording, export, and fullscreen smoke checks; [plugins/default-web-apps/assets/Weigh_Save/modules/ui-controller.js](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/plugins/default-web-apps/assets/Weigh_Save/modules/ui-controller.js#L15).
2. delete: Internal firmware helpers `setButtonPressConfig`, `getTemperatureDriftCompensation`, `adjustTemperatureDriftCompensation`, `setStableOutputEnabled`, `setTrackingEnabled`, `getTrackingOffset`, `getStableOutputValue`, `setManualTrackingOffset`, `setManualStableValue`, and `drawDriftCompensationInfo`; replace with nothing while retaining active algorithms and USB-bound tuning setters; exhaustive tracked-source and patch searches found definitions only; estimated savings: 78 lines; validating check: firmware compilation plus weighing, tuning-command, and display checks; [src/hds.ino](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/src/hds.ino#L676).
3. shrink: Four export-button branches in each UI controller; replace with `for (const b of [this.exportCSVButton, this.exportJSONButton].filter(Boolean)) { b.disabled = !hasData; enabledClasses.forEach(c => b.classList.toggle(c, hasData)); disabledClasses.forEach(c => b.classList.toggle(c, !hasData)); }`, keeping the existing class arrays; identical behavior across 64 local equivalence cases; estimated savings: 36 lines combined; validating check: both export states, initial class combinations, and missing-button guards; [Weigh Save:281](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/plugins/default-web-apps/assets/Weigh_Save/modules/ui-controller.js#L281), [Dosing Assistant:267](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/plugins/default-web-apps/assets/dosing_assistant/modules/ui-controller.js#L267).
4. delete: ESP-NOW's unregistered `OnDataSent`, `receiveCallback`, `sentCallback`, and their private `formatMacAddress` helper; replace with nothing; no registration or external references exist, and broadcasting uses a separate path; estimated savings: 33 lines; validating check: compilation with ESP-NOW enabled and disabled, plus unchanged broadcast payload behavior; [include/espnow.h](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/include/espnow.h#L63).
5. delete: Both singular `pullOtaSelectRelease` overloads and the `PullOtaManifest` overload of `pullOtaParseManifest`; retain the live release-list parser and separate rollback parser; the fetch path passes `PullOtaReleaseList`, with no singular-overload callers; estimated savings: 24 lines; validating check: pull-OTA contracts, catalog selection, and rollback scenarios; [include/pull_ota.h](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/include/pull_ota.h#L568).
6. delete: Unused battery averaging globals `windowSize`, `batteryLevels`, and global `readIndex`; replace with nothing; no consumers exist, and ADS debug `readIndex` references are unrelated members; estimated savings: 3 lines; validating check: firmware compilation and existing battery cadence/debounce checks; [include/power.h](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/include/power.h#L42).
7. delete: Adafruit GFX and SSD1306 entries in `lib_deps`; retain U8g2 and existing ignore guards; both packages are already ignored and have no source or plugin-patch includes; estimated savings: 2 lines and 2 direct dependencies; validating check: dependency resolution and OLED compilation; [platformio.ini](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/platformio.ini#L73).

## Validation Performed

Passed during the audit:

- `python -B tools/test_pull_ota_contract.py`
- `python -B tools/test_ai_docs_contract.py`
- `python -B tools/test_webapp_contract.py`, without `--buildfs`
- An in-memory Node comparison of the existing export-button methods against the proposed loop: 64 cases across both controllers, enabled/disabled states, button presence, and initial class combinations. This one-off check wrote no files; it is not a committed regression test.
- Raw staged and unstaged diffs, porcelain status, and `git diff --check`: clean at audit completion, before adding this report.

The existing contract checks ran against the audited source, not hypothetical deletion patches. The audit performed no firmware builds, dependency installations, hardware operations, deployments, or releases.

## Later Implementation Checks

Firmware recommendations affect the six `esp32s3*` environments in `platformio.ini`, plus patch-created `esp32s3-pressensor` builds. Check patch application before compiling a changed base:

- `esp32s3`
- `esp32s3-grinder`
- `esp32s3-pm-capable`
- `esp32s3-energy-menu`
- `esp32s3-custom`
- `esp32s3-energy-menu-custom`

For firmware helper and battery-global deletion, preserve normal weighing, calibration, tuning commands, display behavior, battery cadence, and low-battery debounce. Keep live setters `setStableOutputThreshold`, `setTrackingThreshold`, and `setTrackingUpdateInterval`.

OTA reductions need pull-OTA-enabled custom selections and normal builds, with release-picker and rollback coverage; also check feature-disabled compilation. ESP-NOW reductions need explicit enabled/disabled coverage; there is no dedicated callback-deletion test.

For display dependency removal, retain the ignore guards and verify all supported OLED controller branches. No RAM or firmware-size savings are claimed without build measurements.

For web-app changes, preserve asset paths and export formats, run webapp contracts, and smoke-test the affected applications. Regenerate derived catalog metadata through the existing pipeline when required; do not hand-edit hashes.

## Normal-Review Handoff

QC and Dosing Assistant share `decentScalePresets` but store incompatible value shapes. QC stores `preset.settings`; the shared preset manager stores the whole `preset`. Their readers expect different shapes. Cross-app preset loading needs a compatibility-aware correctness review, not consolidation: [QC:419](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/plugins/default-web-apps/assets/Quality_Control_Assistant/quality_control.js#L419), [shared presets:69](https://github.com/decentespresso/openscale/blob/ab4516067c7bdb7f3d19816556793f7bea1e3422/plugins/default-web-apps/assets/shared/modules/presets.js#L69).

This concern is unranked and excluded from savings.

Estimated net: -370 lines, -2 deps possible.
