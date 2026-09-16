# Stable 3.1.14 Pre-Tag Audit

Recorded 2026-09-16. This is preparation evidence, not authorization to tag,
publish, deploy, or flash a device. Outstanding gates are listed below.

## Candidate And Scope

- Firmware candidate: `c8a9c02c4c88ce6bdcd76722178c7420726aa6e3`, on `origin/main`.
- Previous published stable: `v3.1.13`.
- Audited range: `v3.1.13..c8a9c02`, including merged PRs #205, #207, and #206.
- Default release hardware: ESP32-S3, `V8_1`, `PCB: 8.1`, standard `esp32s3` environment.
- Preparation branch: `odev/stable-3.1.14-preflight`, in a separate worktree.
- Existing dirty workspace was not changed. No version edit, release tag, workflow
  dispatch, release object, production signature, deployment, or device flash was made.
- `include/config.h` already defaults to `3.1.14`. That is not proof of a published
  release. The release workflow injects and verifies the approved tag version.

The release includes custom builds/fleet installation, optional scale-top taps,
Wake-on-Weight, OTA recovery and publication changes, transport/idle fixes, and
weight-driven auto-off refresh. Energy Saving Beta remains custom/optional;
Light Sleep has one on/off control with button-wake performance boosting.

## Verified Automation

[Candidate CI run 35085444128](https://github.com/decentespresso/openscale/actions/runs/35085444128)
passed Python/Node contracts, native unit tests, standard firmware and LittleFS,
Energy Menu firmware, real-AceButton wake/migration regression checks, and
`release-ready`. The vendored partition table matched the framework.

All 49 `tools/test_*.py` scripts passed locally after making Git for Windows
OpenSSL available on PATH, with one pre-tag accommodation: `test_plugin_catalog.py`
normally reads the absent `v3.1.14` tag. Its prospective tag file reads were mapped
in memory to the candidate checkout; existing preview tag reads were unchanged.
No release tag was created. CI performs its equivalent in its disposable checkout.
The unadapted local catalog test remains expected to fail until the tag exists.

`node --test cloudflare/custom-build-worker/test/*.test.mjs` passed 47 tests.
`tools/check_energy_ble_redraw.py` and local button/migration runtime checks with
AceButton 1.10.1 also passed. The initial OpenSSL-dependent
failures and skipped signing test passed on rerun with OpenSSL on PATH.

Candidate CI dependency inventory records PlatformIO 6.1.19, pioarduino
55.3.311, Arduino-ESP32 3.3.11, and the pinned library versions. The standard
build has two existing deprecated volatile-increment warnings in WiFi setup;
they did not prevent the build. No dependency or firmware change was needed.

Local `pio run -e esp32s3-custom` and its `-t buildfs` target passed for both
the repository selection (`pull-ota` plus `default-web-apps`) and a minimal
selection with no features or plugins. The isolated core is under ignored
`.pio.nosync/core`; no shared core was repaired or overwritten.

| Local custom selection | Firmware bytes | LittleFS bytes | Static RAM |
| --- | ---: | ---: | ---: |
| Pull OTA and default web apps | 1,686,336 | 1,572,864 | 56,236 |
| Minimal BLE/USB | 906,576 | 1,572,864 | 39,620 |

These are direct feature-composition compile checks, not signed custom builder
outputs. The full builder/signing pipeline is covered by its runtime contracts
and the existing live build below; no new production custom build was dispatched.
Native, standard, and Energy Menu builds were verified from exact-candidate CI
rather than repeated locally. No hardware test was performed in this audit.

CI and local artifacts are validation evidence, not the final signed release.

## Official WiFi OTA

The published `v3.1.13` manifest/signature and both OTA images were downloaded.
The signature verifies against committed official Key 1. Both binary lengths
and SHA-256 hashes match the signed metadata. The three official public keys
and partition CSV are unchanged from 3.1.13.

An unsigned prospective `v3.1.14` catalog was generated from the exact candidate's
CI images and the verified previous catalog. Existing `verifyCatalog()` and
`verifyCompatibility()` checks passed:

| Property | Result |
| --- | --- |
| Catalog history | `3.1.14`, then unchanged `3.1.13` |
| Catalog size | 2,422 bytes, below 16 KiB |
| Firmware image | 1,721,072 bytes |
| OTA app slot | 3,342,336 bytes; 1,621,264 bytes spare |
| LittleFS image | 1,572,864 bytes, exact partition size |
| Embedded candidate version | Exact `FW: 3.1.14` string |
| Minimum source | `3.0.0`, permitting 3.1.13 |
| Forward recovery | `1` |
| Hardware/partition contract | Matches the previous stable release |

These checks support the production 3.1.13-to-3.1.14 OTA path once the new signed
assets are published as latest. They do not prove an actual device update.
The unmodified 3.1.13 picker cannot install an unpublished candidate.

Preview.4 has public signed recovery assets; its manifest verifies with official
Key 1. Non-custom preview.3 has only a ZIP and dependency inventory, no signed
recovery manifest; use USB to reach the newer recovery implementation. Hosted
custom preview builds have separate signed assets and are not the same case.

The standard menu installs official firmware. The Custom Build menu installs the
assigned custom combination. Builds without Pull OTA require USB for complete
firmware-and-filesystem updates; optional ElegantOTA accepts firmware only.
Preserve all source recovery assets; do not assume that a current
custom-to-custom or custom-to-official transition has the same recovery behavior
as the older 3.1.13 source firmware.

## Custom Build Service

The live Pages catalog and candidate catalog have the same revision:
`88803f7d532ac74d47624e2c5d6450c195ef5d742ec82abf4dc8153cd8ebad74`.
Live status requests with that revision accepted `main` and `v3.1.14-preview.4`.
Their selected uncached combinations returned `missing`, not `catalog_stale`.
`v3.1.14` returned `firmware_ref_unavailable`, expected while its tag is absent.
No new build request or workflow dispatch was made.

The current Pressensor patch passes `git apply --check --whitespace=error` on
the candidate. This proves applicability, not a full plugin hardware test.

A previously published custom build was downloaded from the live Worker:
`945b9ff4993b40eac3d2a9baf318f3a632a3fdaa7e96fe62ba6a7684b9705382`.
Its custom Key 1 signature, binary hashes/sizes, four-image USB ZIP, dependency
inventory, provenance, firmware identity, and complete-cache contract all verify.
It uses source/builder `9dbe8f5003fc8de5098ebff23c5d78aa446da001`, not this candidate.
[Build run 34508793019](https://github.com/decentespresso/openscale/actions/runs/34508793019)
therefore demonstrates the deployed signing/upload/download pipeline, not a new
stable-tag build. Its features include WiFi, mDNS, both OTA paths, WebSocket,
webserver, LittleFS, and default web apps.

## Documentation Preparation

Reviewed repository guidance and AI build, firmware, display, GPIO, protocol,
storage, OTA, plugin, and release notes against the affected source/workflows and
focused checks. Updated stale release publication instructions, menu paths,
filesystem recovery guidance, same-numeric-version preview selection, storage
ownership/migration notes, and the newly merged power/button behavior.
Added user-facing Wake-on-Weight and auto-off notes without changing defaults.

## Remaining Gates

- Merge these documentation changes and record the resulting approved commit.
  Reconfirm candidate CI for that commit; this audit pins the firmware source above.
- The GitHub `firmware-release` environment exists but has no protection rules or
  deployment branch policy and allows admin bypass. This means GitHub adds no
  approval checkpoint or branch restriction to jobs using that environment.
  The workflows still check `main`, the approved commit, signatures, and assets.
  Admin bypass would let administrators skip environment protections if those
  were configured; it does not skip the checks in the workflow code. This is an
  accidental-publication safeguard to configure, not a firmware or OTA defect.
  Configure the approvals and branch restrictions required by the checklist,
  or explicitly revise that policy. No rulesets were returned; the
  branch-protection endpoint returned 404.
- Official/custom signing and upload secrets are present by name. Existing signed
  assets verify against Key 1. Current private-key matching is enforced during
  signing, but secret values and offline backups were not inspected.
- Record exact-candidate hardware coverage for weighing/calibration, buttons,
  BLE/WiFi, charging, sleep/wake, settings preservation, and advertised plugins.
  The user's previous PR #205-#207 validation is not expanded into an unreported
  full-release OTA/recovery sign-off.
- After separately authorized tagging, run the existing preparation workflow,
  verify its signed draft and evidence, and test those exact bytes on hardware.
- Complete the stable custom-source/default/Worker cutover only after the tag
  exists. Validate minimal, normal, and advertised optional combinations with
  the tagged source and current trusted builder before publication.
  The prepared configurator defaults to stable and offers only stable and main.
  Main requires a testing-only confirmation before build requests. Keep these
  UI changes in the draft PR until the stable service is ready; preview backend
  compatibility and existing recovery assets remain unchanged.
- Test pairing, assignment, progress, failure, completion, already-installed
  behavior, interrupted firmware/filesystem writes, restore failure, and repair.
  Neither fleet nor device state was changed during this audit.
- After separately authorized publication, test the actual production
  3.1.13-to-3.1.14 OTA transition and supported custom transitions immediately.

No confirmed software blocker was found in the checks recorded here. Hardware,
approval-policy, final signed-byte, and stable-service-cutover gates remain open;
this is not an unconditional release-ready sign-off.
