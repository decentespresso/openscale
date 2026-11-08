# 3.1.16 Pre-Tag Preparation

## Candidate And Authorization

Preparation checkpoint: 2026-11-08. This release is not tagged or published.

| Item | Status |
| --- | --- |
| Source baseline | `ac3df004e5856c6f84cad2a5ff89ba037ea058c6`, verified `origin/main` |
| Previous stable | `v3.1.15`, `a8a5a2cf8e3a30addc4f557bcc2772d180cdedf0` |
| Audited range | 80 commits; 174 files, 18,345 insertions, 778 deletions |
| Preparation branch | `odev/prepare-3-1-16-readiness` in a separate clone |
| Service-support branch | `odev/stage-3-1-16-custom-support`; merge only after the real tag exists |
| Public-default branch | `odev/activate-3-1-16-custom-default`; activate only after publication and service readiness |
| Final release candidate | Pending review, required CI, authorized merge, and resulting full main SHA |
| Version | Source base is `3.1.16`; regenerated main custom metadata reports `3.1.16-main-custom` |
| Signed draft, evidence, asset hashes | Not created |
| Custom service | Staged only; activation and the 3.1.16 public default remain required release work |

The user authorized local preparation, documentation updates, custom-build staging,
focused validation, and opening the reviewed release PR. Merge, release tagging,
workflow dispatch, publication, device flashing, and service deployment need
separate authorization. The original feature-branch checkout and its uncommitted
work are outside this release.

The release completion requirement includes advancing main's base version,
`3.1.16-main-custom` for main custom builds, `3.1.16-custom` for tagged custom
builds, the published stable as the public default, and signed official WiFi OTA.
The earlier staging-only boundary describes work performed, not a completed
release. The pre-tag release PR now advances the source base and main metadata;
no live default change or deployment has occurred. Keep the execution approvals
above in place.

The release delivery consists of [PR #278](https://github.com/decentespresso/openscale/pull/278)
and two linked draft change sets: [tagged custom support](https://github.com/decentespresso/openscale/tree/odev/stage-3-1-16-custom-support)
and [public-default activation](https://github.com/decentespresso/openscale/tree/odev/activate-3-1-16-custom-default).
Keep their PR dependencies and hold conditions in #278. Review the later phases
before release approval; do not treat local-only preparation or passing CI as
published, production-signed release or service validation.

Validation used a prospective `v3.1.16` tag at the source baseline inside the
separate clone only. The fixture is removed before handoff; no release tag is
created or pushed. Never push prospective tags. CI creates missing prospective
tags only in disposable validation checkouts; production builds require the real
remote tag.

## Draft Release Notes

These notes describe the upcoming firmware, not an already published release.

- Use one firmware image on 8.x and 9.0.5 boards. The 9.0.5 path adds BQ27427
  fuel-gauge support, Battery Protect, capacity settings, and cooperative startup
  with persisted gauge-cleanup and rollback safeguards.
- Restore manual USB standby, charging menu access, consumed-wake handling, and
  Light Sleep click expiry. Keep charging protection active during supported
  sleep and shutdown paths.
- Add opt-in BLE App Text with scrolling, local cancellation, and a browser
  integration example. The timer and higher-priority scale screens retain control.
- Allow custom firmware to include Pressensor and Grind by Weight together, with
  saved integration settings and guarded switching/shutdown. Older firmware keeps
  its original plugin conflicts.
- Refresh stale ADC samples before tare, preserve queued timer-reset effects,
  and improve ADS diagnostic framing and reset-status reporting.
- Preserve exact WiFi credentials, ask before removing surrounding spaces, and
  recover setup after a lost activation request.
- Correct quality-control/dosing history, presets, and reconnect behavior; improve
  custom-build fleet removal, browser-storage recovery, budget retries, and USB
  install selection preservation.
- Block incompatible legacy downgrades and preserve failed update targets in
  rollback reports.

## Documentation Audit

The audit compares guidance with the release range and current source. A passing
contract supplements this review; it does not certify every documentation claim.

Human-readable updates cover the README, App Text integration guide, board/gauge
instructions, WiFi OTA guide, and affected webapp guides. They distinguish 3.1.16
capabilities from older releases and from the deployed custom service.

The AI audit updates build, firmware, display, GPIO, storage, protocol, plugin,
and repository-routing guidance. OTA, release, and web-UI guidance already match
the affected source and remain unchanged. Historical release records, `AGENTS.md`,
and `CLAUDE.md` remain intact. The plugin compatibility note belongs with the
post-tag service-support change set, not the documentation-only candidate.

## Custom-Build Staging

The backend ref list, plugin manifests, Worker allowlist, and generated catalogs
include `v3.1.16`. A frozen release-specific Pressensor patch matches the current
main patch. Both integration manifests permit the new combined selection only for
`main` and `v3.1.16`, preserving older mappings and conflicts.

Stable custom metadata reports `3.1.16-custom`. Both catalogs share the regenerated
revision. Browser options remain `v3.1.15`, `v3.1.14`, and `main`, with `v3.1.15`
selected by default; `BUILDER_REF` stays `main`. No hosted tagged build or deployment
has been verified for 3.1.16.

The frozen Pressensor patch SHA-256 is
`9544f92b459288f332038e43cd061e4984575e32aa14ce2c436b603b4ebb80b6`,
identical to the baseline main patch. The staged catalog revision is
`4338913880a8af465c8bafaf984039a65ecbf89b5cee063c4f9c81358aedaf8e`.

Keep service-support changes unmerged until the real remote tag exists. Production
builds use the moving `main` builder and validate metadata for every backend ref;
merging a new ref before its tag exists would also break existing hosted builds.
Validation-only prospective tags do not fix this production ordering constraint.

The pre-tag branch advances `include/config.h` to `3.1.16`, regenerates main
metadata, and checks the `3.1.16-main-custom` identity. Review and pass required CI
before freezing the final main candidate. Then approve and create the official
tag on the resulting main commit. Merge the
staged tooling, workflows, plugin manifests/patch, catalogs, Worker configuration,
and related tests only after that tag exists. Regenerate catalogs against the real
tag and recheck patch application before this second merge. Worker deployment and
browser activation remain required release phases with the recorded execution
authorization, not optional follow-ups.

## Validation Evidence

| Evidence | Source And Result |
| --- | --- |
| Existing firmware CI | [Run 42058007291](https://github.com/decentespresso/openscale/actions/runs/42058007291), baseline SHA: passed native, Python/Node, standard/grinder, energy-menu, and release-ready checks |
| Existing custom-build CI | [Run 42088611134](https://github.com/decentespresso/openscale/actions/runs/42088611134), baseline SHA: build job passed; skipped plugin/minimal jobs provide no coverage |
| Preparation contracts | Passed AI documentation, plugin catalog, plugin CI, custom execution, and custom cache transport contracts |
| Final generated catalogs | Regenerated after bundled README edits; catalog contract passed |
| Worker/configurator | `node --test cloudflare/custom-build-worker/test/configurator.test.mjs cloudflare/custom-build-worker/test/worker.test.mjs`: 34 passed, none skipped |
| Documentation-only snapshot | Baseline-ref catalog generation, AI documentation contract, catalog contract, and whitespace checks passed without adding 3.1.16 support |
| Raw review | Full preparation patches reviewed; `git diff-files --check` passed, including the frozen patch |
| Pressensor profile | Passed `esp32s3-pressensor` on the baseline plus frozen patch; RAM 58,652 bytes, flash 1,771,093 bytes |
| Combined profile | Passed `esp32s3-custom` firmware and LittleFS with `v3.1.16`, Pull OTA, Pressensor, Grind by Weight, and default web apps; embedded `FW: 3.1.16-custom` verified |
| Version correction | Main base and metadata advance together; prior stable custom identities remain unchanged |
| Independent re-review | No remaining pre-tag PR blocker after fixing the main version and metadata; post-tag release gates remain open |
| Preparation CI at `2bc688c` | Runs 42509197194, 42509197220, and 42509197296 passed; later workflow/default and delivery changes require their own head checks |
| Previous stable input | `Release firmware` defaults to `v3.1.15`; the signed catalog history floor remains `v3.1.13` and the runtime previous-stable guard remains active |
| Final-candidate CI | Pending required CI on the release PR before an authorized merge; baseline results do not certify preparation changes |

Local builds use a task-isolated PlatformIO core, pinned dependencies, and
sequential PlatformIO commands. They do not sign, publish, upload, or flash.
The initial long-path build failed at linker-script generation with the Windows
command-length limit. A short source path alone still failed. Validation then
uses physical source paths under `D:\r316`, core `D:\r316\c`, and
`PLATFORMIO_BUILD_DIR=D:\r316\b`; the global core remains untouched. Raw local
diagnostics stay under ignored `build/validation/` in the preparation clone.
The platform-freshness advisory reports a newer platform; this preparation keeps
the reviewed `55.03.311` pin and does not mix a dependency upgrade into the release.

The completed raw logs are `build/validation/pressensor-short.log` and
`build/validation/combined.log`. Review found the existing `wifi_setup.cpp`
volatile-increment warnings and upstream ESP-SR/RainMaker/USB const-qualifier and
unused speech-partition advisories, with no compiler, linker, or filesystem error.
The combined profile uses 60,140 bytes of static RAM and 1,765,401 bytes of flash.

Unsigned combined-profile images, not official release or production custom OTA
assets, are retained in `build/validation/combined-output/`. The raw log records
all four image sizes and hashes. Its source is the baseline plus the frozen patch
and staged builder/assets, not a future merged candidate or signed draft.

| Local Image | Bytes | SHA-256 |
| --- | ---: | --- |
| `firmware.bin` | 1,811,440 | `87c54013ec373ed9dd146da1d6b4b035ae6b4579c532f075b26b368dbcce7203` |
| `littlefs.bin` | 1,572,864 | `363c9ebc65bcc3f84fbd0cc3f397925c48ae1240a709df3ee21668b87ccbb725` |

The Pressensor row records compilation only. Its image was not retained when
PlatformIO switched projects in the shared short build directory; it is not
evidence of an exact-byte hardware installation.

The focused Python commands are `tools/test_ai_docs_contract.py`,
`tools/test_plugin_catalog.py`, `tools/test_plugin_ci_contract.py`,
`tools/test_custom_build_execution.py`, and `tools/test_custom_cache_transport.py`.
Each ran with Python 3.11. Git fixture creation required sandbox escalation; tests
then passed. This environment rejects porcelain working-tree diff inspection, so
raw `git diff-files -p` and `git diff-files --check` supplied review and whitespace
evidence without changing Git configuration.

No hardware tests ran during this preparation. Battery/charging behavior on 8.x
and 9.0.5, gauge cleanup across OTA/rollback, USB standby/wake, and integration
shutdown/switching still need applicable evidence or explicit acceptance of gaps.
Host checks do not establish charging safety.

## Remaining Release Gates

1. Review the source base-version update to `3.1.16`, regenerated main metadata,
   and version-identity checks included in preparation. Pass relevant required
   CI. After merge authorization, merge and record the resulting full main SHA.
   Keep the service-support change set unmerged. Recheck main,
   latest stable, tag availability, and the frozen patch before accepting it.
2. After tagging/draft authorization, create immutable `v3.1.16` on that candidate.
   Confirm required signing configuration and respect environment protections
   without printing keys or changing repository settings.
   Dispatch `Release firmware` from `main` with `expected_commit`, the current
   latest stable as `previous_tag`, and `bootstrap_catalog=false`.
3. Verify the draft's eight assets: `HDS_FW_v3.1.16.zip`, `firmware.bin`,
   `littlefs.bin`, `dependencies.txt`, `manifest.json`, `manifest.sig`,
   `release-evidence.json`, and `release-evidence.sig`. Record the evidence
   SHA-256 and verify signatures, ZIP images, versions, hashes, compatibility,
   recovery metadata, and notes. Do not rebuild or replace approved bytes.
4. Before authorized hardware acceptance, verify device identity and take a fresh
   protected backup. Check exact signed firmware/LittleFS readback, boot/version,
   saved network, and protected settings. Preserve NVS, calibration, pairing,
   credentials, and partition layout. Record unavailable tests and accepted gaps.
5. After publication authorization, dispatch `Publish firmware` with the same
   tag, candidate, and approved evidence digest. Verify stable/latest status and
   asset identity. Check production older-to-newer OTA after publication, or
   record it untested; production firmware cannot read draft-only assets.
6. After the real tag exists, merge and activate tagged custom support, verify a
   real signed `3.1.16-custom` hosted build and a `3.1.16-main-custom` build, then
   deploy the public selector with 3.1.16 as its stable default after publication
   and service readiness. Retain prior stable refs and `main`. Verify the live
   options/default before reporting the full release complete.

Custom-service deployment and public selector/default activation have not run;
they remain part of the full release and need execution authorization. Scale
assignments and security-setting changes remain excluded. Respect configured
protections; do not change repository secrets, variables, or security settings.
