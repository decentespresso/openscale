# AI Release Notes

Read this when preparing a stable firmware release. This file defines the
release audit; it is not the user-facing changelog.

## Core Rule

Audit all AI guidance before a release, but update only the notes affected by
changes since the previous stable release.

A passing documentation contract validates selected invariants only. It does
not prove that all prose is current.

Do not create or push a tag, dispatch a workflow, publish a release, or change
repository secrets or variables without explicit user authorization.

## Release Model

`main` is the releasable branch. Stable releases use a numeric `vX.Y.Z` or
`X.Y.Z` tag on a known-good commit reachable from `main`.

`.github/workflows/release.yml` is the source of truth for release mechanics.
The current workflow is for stable releases, not preview, RC, or prerelease
tags.

For OTA signing, manifests, compatibility, picker, and rollback invariants,
also read `docs/AI_OTA_NOTES.md`.

## 1. Establish the Release Range

Fetch the remote state and identify the previous applicable stable tag:

```sh
git fetch origin main --tags
git status --short
git log --oneline <previous-release-tag>..HEAD
git diff --stat <previous-release-tag>..HEAD
git diff --name-status <previous-release-tag>..HEAD
```

Confirm that:

- the intended commit is reachable from `origin/main`;
- the working tree has no unintended changes;
- the version tag does not already identify another commit;
- all intended changes, and no unrelated local commits, are included.

Do not infer release scope from generated GitHub notes alone.

## 2. Audit AI Guidance

Use the release diff to determine which notes need detailed review. Run the AI
documentation contract across the complete set.

| Changed area | Review |
| --- | --- |
| Repository-wide agent rules | `AGENTS.md` |
| Paths and subsystem entry points | `docs/AI_REPO_MAP.md` |
| PlatformIO, build, flash, serial, LittleFS | `docs/AI_BUILD_NOTES.md` |
| Runtime, callbacks, tasks, timers, weighing, display | `docs/AI_FIRMWARE_NOTES.md` |
| Pins, boards, electrical behavior, wake, sleep holds | `docs/AI_GPIO_NOTES.md` |
| BLE, USB, WebSocket, Decent binary, integrations | `docs/AI_PROTOCOL_NOTES.md` |
| NVS, defaults, schemas, migration, reset | `docs/AI_STORAGE_NOTES.md` |
| OTA, signing, manifests, assets, picker, rollback | `docs/AI_OTA_NOTES.md` |
| Tagging, release workflow, checks, publication | this file |

For each affected note:

1. Verify statements against current source, workflow, and tests.
2. Remove stale paths, symbols, commands, constants, and behavior.
3. Add new safety or compatibility constraints.
4. Prefer canonical definitions over duplicated exact values.
5. Protect useful duplicated facts with focused contract tests.
6. Do not edit unaffected notes merely to mark them reviewed.

Update `CLAUDE.md` only if its routing changes; do not duplicate `AGENTS.md`.

## 3. Review User-Facing Documentation

Review `README.md` and related documentation when the release changes:

- setup, provisioning, discovery, or credentials;
- supported hardware;
- weighing, calibration, buttons, display, power, or charging;
- BLE, USB, WiFi, WebSocket, or grinder integrations;
- settings, defaults, migration, or reset behavior;
- build, flashing, recovery, OTA, downgrade, or rollback;
- required manual actions and known limitations.

The workflow publishes generated GitHub notes after its checks. Ensure PR titles
and user-facing documentation are accurate before dispatch.

Do not expose signing-key material or non-public credentials.

## 4. Verify Release Invariants

Inspect `.github/workflows/release.yml` and its tools directly. Confirm that the
release still:

- validates a stable numeric tag;
- requires the tag commit to be reachable from `main`;
- checks out and builds the tagged commit;
- builds `firmware.bin` and `littlefs.bin`;
- produces the legacy firmware ZIP;
- requires the OTA signing key and matching public key;
- verifies the previous signed catalog before reuse;
- generates and verifies the new signed catalog;
- uploads all required assets in the intended order;
- publishes only after asset and signature checks pass.

Do not hand-edit generated manifests or signatures. Do not weaken HTTPS,
signature, hash, size, schema, compatibility, or rollback checks.

Keep previous signed assets available when OTA history, downgrades, or rollback
filesystem recovery require them.

## 5. Required Validation

At minimum, run:

```sh
python tools/test_ai_docs_contract.py
python tools/test_release_workflow_contract.py
python tools/test_generate_release_manifest.py
python tools/test_pull_ota_contract.py
python tools/test_ota_rollback_contract.py
python tools/test_ota_public_key_header.py
pio run -e esp32s3
pio run -e esp32s3 -t buildfs
git diff --check
```

Also run the focused checks from every affected topic note.

Record skips, unavailable tools, and reduced coverage. Local artifacts are
validation evidence, not substitutes for workflow outputs.

## 6. Hardware Validation

Documentation and contract tests do not replace device testing.

Record the tested board and PCB variants, commit, firmware version, flash or OTA
path, filesystem image, peripherals, procedure, result, and untested supported
configurations.

Changes affecting OTA should test an applicable older-to-newer update path when
practical. Test rollback or interrupted-update behavior when those paths
changed.

Run the corresponding device checks for changes affecting weighing,
calibration, power, wake, charging, buttons, display, BLE, WiFi, WebSocket, or
grinder control.

## 7. Pre-Tag Gate

Before tagging, confirm:

- the exact release commit is on `main`;
- required CI, local, and hardware checks passed;
- limitations or untested configurations were accepted explicitly;
- affected AI notes and user-facing documentation are current;
- required secrets and variables are configured without printing values;
- the intended version is consistent across release inputs and outputs;
- no unresolved migration, OTA, asset, or hardware blocker remains.

## 8. Release And Post-Release Verification

Only after explicit authorization:

1. Create and push the stable tag on the approved commit.
2. Dispatch `Release firmware` with that exact tag.
3. Inspect the complete workflow result and raw failures.
4. Confirm the release points to the intended commit.
5. Confirm the legacy ZIP and required OTA assets are present.
6. Confirm the release is neither draft nor prerelease.
7. Check the manifest version, hardware, environment, partitions, assets, and
   minimum supported source version.
8. Verify installation through relevant user and OTA paths.
9. Record limitations or rollback actions.

Do not delete or replace published assets without evaluating signed catalog,
downgrade, and rollback consequences.

## Completion Report

Report:

- release version, exact commit, previous tag, and audited range;
- AI guidance reviewed and changed;
- user-facing documentation changed;
- automated commands and results;
- hardware and update paths tested;
- published assets verified;
- skipped checks and untested configurations;
- limitations and blockers;
- tag, workflow, and publication actions performed.

Separate verified facts from assumptions. A clean build alone is not sufficient
evidence that the release is ready.
