# 3.1.15 Pre-Tag Preparation

## Candidate And Outcome

On 2026-10-10, the original candidate stopped at the credential-preservation
gate before tagging, workflow dispatch, or flashing. The user subsequently
authorized a reviewed credential fix and its merge after current-head CI passes.
The current authorization ends immediately before tagging or release dispatch.

| Item | Result |
| --- | --- |
| Candidate | `02bc36d1f9f7daa2403a9982102f4d2ce8c2bda7` |
| Previous stable | `v3.1.14`, commit `b450f03e1d48fa516882fc39d02340b0b96c9397` |
| Remote baseline | `v3.1.14` remains the latest published stable |
| Candidate ancestry | Reachable from `origin/main` |
| Release range | 101 changed files, 8,121 insertions, 1,496 deletions |
| Isolated branch | `odev/prepare-3-1-15-readiness` |
| Review changes | [PR 228](https://github.com/decentespresso/openscale/pull/228); credential fix and reviewer regressions, awaiting current-head CI and authorized merge |
| Release tag | `v3.1.15` did not exist at the preflight check; no tag created |
| Draft link | Unavailable: preparation stopped before draft creation |
| Signed evidence digest | Unavailable: no release artifacts or evidence signed |
| Original workspace | No tracked or untracked files edited by this preparation |

## Credential Preservation Blocker

The original candidate's `WiFiParams::saveCredentials()` in `src/wifi_settings.cpp`
writes a new `wifi/credentials` blob and verifies it. For a nonempty SSID,
it leaves the legacy `wifi/ssid` and `wifi/pass` values unchanged.

Published `v3.1.14` reads only those legacy strings in
`WiFiParams::init()` in `src/wifi_setup.cpp`. After a successful candidate
network switch, downgrading therefore selects the previous network rather
than the saved current network. With fresh candidate provisioning and no
legacy strings, the older firmware reads empty credentials. The latter case
follows from source inspection; this run did not exercise it on hardware.

The original `docs/AI_STORAGE_NOTES.md` documents this limitation. It conflicts
with this preparation's requested credential preservation and safe downgrade
coverage. Passing current-version persistence tests does not cover it.

The initially added `testCredentialsSurviveStableDowngrade()` reused the existing
Preferences mock and compiled the candidate's actual settings implementation.
It verified a successful save and candidate reboot read, then checked the
same legacy-key reads used by published `v3.1.14`.

The reproduction was run from the isolated checkout:

```sh
python tools/test_wifi_setup_runtime.py
```

The original candidate's raw diagnostic was:

```text
Assertion failed: stable.getString("ssid", "") == "New network"
```

This failure remains evidence against the original candidate. The authorized
fix below does not change that candidate's bytes. Further release preparation
requires a reviewed, newly authorized candidate SHA.

## Authorized Credential Fix

The user authorized correcting credential preservation, pushing the isolated
branch, and opening a review PR. The later authorization permits merging only
this fix after the requested reviewer has no unresolved findings and current-head
CI passes. Additive custom-service support stays separate, unmerged, and
undeployed. The original dirty workspace remains untouched. Commit and current
CI identities are recorded in the review PR rather than self-referencing this
commit. No tag or release dispatch is authorized at the current boundary.

- `saveCredentials()` verifies a previous atomic blob, writes and reads back
  the legacy pair, then commits the new blob. RAM changes only on success.
  Failed saves attempt verified restoration; startup repairs interrupted legacy
  writes from the authoritative blob.
- `prepareLegacyDowngrade()` verifies or repairs legacy credentials and removes
  the blob only after verification. Signed OTA applies this gate when the target
  or known rollback version predates `3.1.15`, before pending LittleFS state or
  firmware writes. Failure cancels installation.
- The surviving legacy keys allow `v3.1.14` to connect and a later upgrade to
  read network changes saved by that older firmware. Existing namespaces for
  calibration, pairing, settings, and the device name are not cleared.
- Raw USB and firmware-upload downgrades bypass the signed gate. Successful
  saves mirror the current credentials, but subsequent older-firmware network
  changes can still be overridden by a surviving blob on re-upgrade. Arbitrary
  unmanaged round-trip compatibility is not claimed.
- Runtime regressions exercise fresh provisioning, open networks, legacy
  reads, downgrade/re-upgrade network changes, failed and falsely acknowledged
  writes, simulated interruption, reset, restoration failure, and OTA abort
  before state, stream, or completion-reporting work.

No source-version bump, public custom-build selection/default change, protocol
change, service staging, or security-policy change is included.

The requested GPT-6.1 Sol xhigh reviewer identified two issues in the first fix:
legacy read errors could resemble a valid empty password, and a failed blob
presence query could skip removal. Regression checks reproduced both before
their fixes. Verification now uses a nonempty error sentinel for legacy reads,
and downgrade preparation always requires verified anchoring and actual blob
removal. The reviewer independently reran the runtime harness and found no
additional PR blockers.

The reviewer also confirmed a pre-existing empty-reset limitation: false-negative
legacy presence queries can leave one legacy key behind. The authoritative empty
blob suppresses it, and healthy startup repairs it. This is not a newly reachable
signed-install bypass; unmanaged downgrades retain their stated limitation.
Physical power-loss and concurrency validation remain outstanding.

## Verification Results

| Check | Result |
| --- | --- |
| Candidate CI | [Build firmware run 37984757708](https://github.com/decentespresso/openscale/actions/runs/37984757708) succeeded |
| CI Python/Node contracts | Passed in the candidate run |
| CI native unit tests | Passed in the candidate run |
| CI standard/grinder firmware and filesystem | Passed in the candidate run |
| CI energy-menu firmware | Passed in the candidate run |
| CI release-ready | Passed in the candidate run |
| Initial fix CI (`dd10c0c78922873c6706f858b052b538d1b8e9d9`) | [Firmware](https://github.com/decentespresso/openscale/actions/runs/38042636180), [custom](https://github.com/decentespresso/openscale/actions/runs/38042636167), and [OTA contracts](https://github.com/decentespresso/openscale/actions/runs/38042636122) passed; these runs do not cover subsequent reviewer fixes |
| Reviewer fixes | Nine focused Wi-Fi/OTA/custom-reporting/docs checks passed, including expanded runtime independently confirmed by reviewer; fresh CI pending |
| Reviewer fixes: firmware/filesystem | Standard builds passed; RAM 57,972 bytes, firmware usage 1,722,381 bytes; firmware SHA-256 unchanged by filesystem generation |
| Existing local Wi-Fi runtime harness | Passed before adding the missing downgrade check |
| Original candidate downgrade regression | Failed with RTK and again with raw diagnostics |
| Authorized fix: Python checks | All 63 existing `tools/test_*.py` scripts passed, including expanded credential/OTA regressions |
| Authorized fix: Worker/browser Node suite | 50 passed |
| Authorized fix: native unit tests | 94 passed across 7 suites with the isolated normal core |
| Authorized fix: standard firmware build | `pio run -e esp32s3` passed; RAM 57,972 bytes, firmware usage 1,722,413 bytes |
| Authorized fix: standard filesystem build | `pio run -e esp32s3 -t buildfs` passed; firmware SHA-256 unchanged |
| COM5 discovery | USB-SERIAL CH340K, VID:PID `1A86:7522`, USB location `1-7` |
| COM5 scale identity | User confirmed intended test scale and USB recovery; esptool identified ESP32-S3 v0.2, MAC `cc:ba:97:33:27:f0`, 16 MB flash; running firmware/settings still need inspection |
| COM5 original-flash backup | Read-only capture completed: 16,777,216 bytes, SHA-256 `6021bbc28dc7c3a211cbf323b1e710c445d2e02738b0855dba26bf4eed9043d6`; ignored storage with protected ACL, never uploaded |
| COM5 flashing and NVS | No flash, erase, credential reset, calibration write, or pairing mutation |

The CI rows distinguish the original candidate, the initial fix, and later
reviewer fixes. Local full-matrix results validate the initial fix, not signed
release bytes. The initial Python dependency
setup failed because `uv` split the absolute `UV_CONSTRAINT` path at the space in
`El Fuzzi`. A relative constraint path resolved it without changing dependency
pins or the global cache. The build retained the existing platform-freshness
advisory and volatile-increment warnings in `src/wifi_setup.cpp`.

Unsigned local image hashes, not release assets or a signed evidence digest:

| Image | SHA-256 |
| --- | --- |
| Standard firmware, before and after filesystem generation | `ac74ba31a9c0b523c4de8337a2630dcdc6dceeaa363fb578969858b140df117e` |
| Standard LittleFS | `98d71330da9869165029c5462f532efd5f036215cfbf248e7d8419cd97c9009a` |
| Standard firmware after reviewer fixes, before and after filesystem generation | `2435128af154ced71c901cb0b0c52799b08a07b09d0c3af499191ce7abea3cf5` |

The remaining isolated-core
firmware/filesystem matrix, candidate custom profiles, Pressensor/grinder
validation, signed custom manifests, custom binary preservation through
filesystem generation, and custom identity checks remain release-preparation
gates.

USB installation, firmware-upload OTA, abandoned-upload recovery, direct
filesystem rejection, pairing/assignment/custom OTA/completion, signed pull
OTA to `v3.1.14`, interrupted-update recovery, network switching/rollback,
provisioning, and OTA exclusion during switching remain unverified on COM5.
The scale retains its pre-existing installation; no final draft was installed.

## Audit And Provisional Release Notes

The release-range audit identified Wi-Fi setup and storage, calibration and
soft-sleep command handling, grinder ADC recovery, dashboard reconnection,
webapp persistence/exports/themes, and plugin presentation/build tooling.
Review covered repository routing and build, storage, OTA, and release guidance.
The remaining subsystem documentation/source audit stopped with the defect.

Provisional user-facing notes, subject to completing that audit:

- Add scanned networks and verified Wi-Fi switching with failed-switch recovery,
  bounded connection attempts, and device-identity checks in setup pages.
- Correct calibration entry, charging/menu interactions, queued USB sleep/wake,
  and grinder stop/error handling around ADC reset and recovery.
- Improve webapp session/history persistence, isolate versioned presets, quote
  CSV fields, and reconnect the dashboard after WebSocket disconnects.
- Add shared theme controls and improve plugin guides, declared webapp assets,
  and custom-build verification.
- Preserve saved Wi-Fi credentials for signed downgrades to `v3.1.14` and later
  upgrades, subject to review and hardware validation of the unmerged fix.

## Remaining Gates

- Review and merge the credential fix, pass CI for the resulting main commit,
  and authorize its exact candidate SHA before any tag or release dispatch.
  The original candidate remains blocked.
- Complete additive builder refs, plugin compatibility/patch mappings, Worker
  allowlists, catalogs, and tests in an unmerged branch. None were changed in
  this run. Retain older refs and recovery assets. Keep public selection and
  defaults unchanged; do not deploy these changes.
- Use existing workflow version injection for `3.1.15`; no source version bump.
  Tagged custom builds must report `3.1.15-custom`. Candidate `main` custom
  builds retain the source development label, currently `3.1.14-custom`.
- Inspect the installed COM5 partition layout and running firmware/settings, and
  establish the preservation procedure before flashing or changing networks.
  User/silicon identity and the protected original backup are verified.
- Verify all eight exact draft assets, both signatures, binary hashes, ZIP
  contents, version injection, signed evidence, and the requested hardware paths.
- Configure required reviewers and main-only branch restrictions on
  `firmware-release` through a separately authorized administrator action.
  The read-only environment inspection returned `protection_rules: []` and
  `deployment_branch_policy: null`; this is a publication blocker.
- Keep production `3.1.14 -> 3.1.15` pull OTA unverified: production firmware
  cannot access draft assets. Keep hosted `v3.1.15` custom-service activation
  pending deployment approval; staged validation is not deployment proof.

No `Release firmware` or `Publish firmware` dispatch, prerelease publication,
tag movement, service deployment, or repository security-setting change occurred.
