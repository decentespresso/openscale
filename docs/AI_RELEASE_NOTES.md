# AI Release Notes

Read this when preparing a stable firmware release. This file defines the
release audit; it is not the user-facing changelog.

## Core Rule

Use the diff since the previous stable release to review affected guidance.
Do not reread or rewrite unrelated topic notes as a release ritual.

A passing documentation contract validates selected invariants only. It does
not prove that all prose is current.

Get explicit authorization for tagging/draft preparation, publication, and service
deployment. A user may authorize several phases together; record the scope once
and do not ask again unless the candidate, risk, or scope changes. Never infer
permission to change repository security settings, secrets, or variables.

Keep one short checkpoint: candidate SHA, artifact hashes, passed checks with
source/run IDs, remaining work and approvals. Resume there, not from the beginning.
"Already tested" or "complete" means retain the evidence, not repeat the test.
Before repeating any completed check, identify the changed input or relevant risk.

## Release Model

`main` is the releasable branch. Stable releases use a numeric `vX.Y.Z` or
`X.Y.Z` tag on a known-good commit reachable from `main`.

`.github/workflows/release.yml` prepares a verified signed draft. It requires
the approved full candidate SHA and expected previous stable tag, and reuses
`nightly.yml` and the minimal custom build from `custom-build.yml` against that
exact candidate before signing. It never publishes automatically.

`.github/workflows/publish-release.yml` separately approves existing draft bytes
using the evidence SHA-256 from a successful preparation run. It rechecks the
tag, signed inventory, previous stable catalog, and draft asset metadata without
rebuilding or signing. Both workflows run from `main` and share concurrency.

Preview/RC tags (`vX.Y.Z-preview.N`, `vX.Y.Z-rc.N`) remain prereleases and never
become latest. Stable publication explicitly selects latest. All releases publish
signed recovery assets; preview/RC entries stay out of the stable OTA catalog.

Respect existing required checks and environment protections; never bypass them.
Additional reviewers and environment branch restrictions are useful safeguards,
not an automatic new release prerequisite for a sole operator. Report their
absence once and record the operator's decision. Do not demand a second person
or change security settings without authorization. Historical release checklists
are reference evidence, not mandatory scripts to replay for every version.

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

Use the release diff to determine which notes need detailed review. Reuse a passing
AI documentation contract for unchanged inputs; run it when guidance changes.

| Changed area | Review |
| --- | --- |
| Repository-wide agent rules | `AGENTS.md` |
| Paths and subsystem entry points | `docs/AI_REPO_MAP.md` |
| PlatformIO, build, flash, serial, LittleFS | `docs/AI_BUILD_NOTES.md` |
| Custom build composition, plugins, service deployment | `docs/AI_PLUGIN_NOTES.md`, `docs/custom-build/operations.md` |
| Runtime, callbacks, tasks, timers, weighing | `docs/AI_FIRMWARE_NOTES.md` |
| OLED, setup menus, layout, display options | `docs/AI_DISPLAY_NOTES.md` |
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
- BLE, USB, WiFi, WebSocket, or Grind by weight integration;
- settings, defaults, migration, or reset behavior;
- build, flashing, recovery, OTA, downgrade, or rollback;
- required manual actions and known limitations.

Preparation generates draft notes. Review them and the user-facing documentation
before approving publication.

Do not expose signing-key material or non-public credentials.

## 4. Verify Release Invariants

Inspect `.github/workflows/release.yml` and its tools directly. Confirm that the
release still:

- validates a stable numeric tag or a supported preview/RC tag;
- requires the tag commit to be reachable from `main`;
- checks out and builds the tagged commit;
- builds `firmware.bin` and `littlefs.bin`;
- produces the legacy firmware ZIP;
- publishes `dependencies.txt` with the tagged PlatformIO version and dependency inventory;
- requires the OTA signing key and matching public key;
- verifies the previous signed catalog before reuse;
- generates and verifies the new signed catalog;
- uploads all required assets in the intended order;
- downloads draft assets and verifies their signatures, manifest sizes/hashes, and agreement with build outputs, including all four USB ZIP images;
- validates model/PCB, chip/environment, flash and partition contracts, canonical URLs, forward recovery, and a minimum source version that permits the previous stable before signing evidence;
- uses `--verify-tag` for both draft creation and publication so missing remote tags are not implicitly recreated;
- signs an inventory binding the candidate, repository, previous stable, preparation run, and all six release assets;
- stops at a verified draft, then publishes only through separately approved evidence from a successful preparation run.

Do not hand-edit generated manifests or signatures. Do not weaken HTTPS,
signature, hash, size, schema, compatibility, or rollback checks.

Keep previous signed assets available when OTA history, downgrades, or rollback
filesystem recovery require them.

## 5. Risk-Based Validation

Required repository checks and the release workflow's built-in preflights still
run unchanged. Reuse successful exact-head CI instead of duplicating its builds
and contracts locally. A merge with an identical reviewed tree does not require
another manual test campaign. Evidence from an older tree may cover unchanged
paths after an explicit diff assessment, but must retain its original SHA.

| Change | Additional focused validation |
| --- | --- |
| Documentation only | `python tools/test_ai_docs_contract.py`, `git diff --check`; no firmware build or hardware test |
| Public version selector/default | Catalog and configurator tests, Pages deployment, live options/default check; do not wait for unrelated optional firmware builds |
| Builder refs, plugin compatibility, Worker catalog | Python/Node contracts and affected profile builds; verify a real hosted build after deployment |
| Firmware behavior | Affected topic contracts and relevant device checks; use existing standard/grinder/energy/native CI results |
| OTA, NVS, recovery or signing | Focused migration/update/failure tests plus release signature, compatibility and recovery checks |

Examples of affected checks: `test_ble_subscription_contract.py`,
`test_mdns_name_contract.py`, `test_ota_reboot_routing_contract.py`,
`test_grinder_feature_flag_contract.py`, `test_gzip_web_assets.py`, and
`pio test -e native`. Release tooling changes also need the release workflow,
manifest, catalog, asset verification, publication and public-key contracts.
Run only missing relevant coverage, not this entire list by default.

Use an isolated checkout/core for local builds and sequential PlatformIO commands
per core. Reuse completed custom-profile results when their source, builder and
configuration remain applicable. Record skips and unavailable configurations.
Local artifacts never substitute for verifying the actual signed workflow assets.

## 6. Hardware Validation

Documentation and contract tests do not replace device testing.

Record the tested board and PCB variants, commit, firmware version, flash or OTA
path, filesystem image, peripherals, procedure, result, and untested supported
configurations.

Choose hardware tests from changed behavior, not a fixed full-release matrix.
Repeat rollback/interruption, pairing, two-network switching or provisioning
only when those paths changed, prior evidence failed, or the user requests it.
Do not silently relabel old tests as exact-candidate or exact-artifact tests.

Before authorized flashing, verify device identity and take a fresh protected
backup. Preserve calibration, credentials, pairing, settings and partition layout;
never erase NVS as release preparation. For final signed draft bytes, the normal
acceptance is one USB firmware/LittleFS installation, exact readback, boot/version,
saved-network and protected-settings check. If those same hashes were already
installed and checked, reuse that result. Different local/CI hashes require this
bounded acceptance, not automatic repetition of the full hardware campaign.

Production firmware cannot pull draft-only assets. Record the real production
older-to-newer pull as a post-publication check, not an impossible prepublication
gate. USB reset is not battery power loss. Report unavailable physical tests and
obtain acceptance of material limitations rather than repeatedly requesting them.

## 7. Pre-Tag Gate

Before tagging, confirm:

- the exact release commit is on `main`;
- required CI and the agreed risk-based checks passed or have explicit exceptions;
- limitations or untested configurations were accepted explicitly;
- affected AI notes and user-facing documentation are current;
- required secrets and variables are configured without printing values;
- the intended version is consistent across release inputs and outputs;
- no unresolved migration, OTA, asset, or hardware blocker remains.

Find candidate defects before tagging where possible. Once pushed, never move
the release tag or silently replace candidate bytes. Stop for review on a defect;
agree a new version/candidate rather than reusing an immutable identity. Use the
workflow's version injection; do not bump source versions just to label artifacts.

## 8. Release And Post-Release Verification

Only after explicit authorization:

1. Create and push the stable tag on the approved commit.
2. Dispatch `Release firmware` from `main` with the tag, `expected_commit`, and
   `previous_tag`. Resolve the latest published stable at runtime; never reuse
   a version-specific example or an old workflow default as the baseline.
3. Inspect the successful preparation run and record its evidence SHA-256.
4. Confirm the draft contains the legacy ZIP, required OTA assets,
   `dependencies.txt`, `release-evidence.json`, and `release-evidence.sig`.
5. Complete or reuse the bounded exact-byte hardware acceptance from section 6.
6. Dispatch `Publish firmware` from `main` with the same tag and commit and the
   approved `evidence_sha256`. Follow any configured environment approval rules.
7. Confirm stable/latest or preview/prerelease classification as appropriate.
8. Verify published asset identity and compatibility. Perform the agreed real
   production OTA check, or explicitly record it as not tested; do not silently
   launch another hardware campaign.
9. Record results, limitations, and recovery actions.

If preparation fails after creating a draft, leave it unpublished. Investigate
before explicitly cleaning up that unpublished draft and rerunning preparation.
Do not bypass the successful-run check with manual publication.

Do not delete or replace published assets without evaluating signed catalog,
downgrade, and rollback consequences.

## 9. Custom-Service Completion

Agree once whether the release includes custom-service activation and public
selection/default changes. Do not hide either as an unmentioned later phase.
Prepare builder refs, plugin compatibility, Worker allowlists and both catalogs
together, retaining older refs, immutable combinations and recovery assets.

When activation is authorized, merge reviewed changes after relevant/required
checks, deploy only changed components and verify the live service with the
published catalog revision. Verify a real tagged build's source/builder commits,
`X.Y.Z-custom` version, production signature, hashes, ZIP and combination identity.
Do not assign it to a scale or repeat OTA tests unless that is in the agreed scope.

Backend support is not visible browser support. When public rollout is authorized,
add the new stable to the selector, make it the agreed default, retain the prior
stable and `main`, and verify the deployed page's actual options/default. A
selector-only follow-up needs no Worker redeployment or firmware build wait.
When asked to keep defaults unchanged, state that limitation clearly in the result.

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
