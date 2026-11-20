# 3.1.17 Pre-Tag Preparation

Checkpoint: 2026-11-20. This record describes preparation, not a tagged or
published release. Keep final candidate and run evidence in the forthcoming
pre-tag PR description and its local report, rather than committing a document's
own source SHA back into the candidate.

## Candidate And Authorization

| Item | Status |
| --- | --- |
| Intended stable | `v3.1.17` |
| Previous stable | `v3.1.16`, `8e2174270df5fbecab136225270e0799e51a63dc` |
| Initial main baseline | `f5474e9e66dcfcedc5bbbbaf8cf4fbf08e915272` |
| Six-PR merged main | `dcb53ac6687d8b7c4a6197a4aee5210e57160814`, tree `aee36574dcb7f63088bba42d718735041ad70d31` |
| Local six-PR integration | Commit `1b74116ef86e9edfe1356eb2fa1371c14ed166ec`, tree `aee36574dcb7f63088bba42d718735041ad70d31`, before version/documentation preparation |
| Release candidate | Pending the pre-tag PR, review, required CI and resulting full main SHA |
| Source version | `3.1.17`; main custom metadata must report `3.1.17-main-custom` |
| Tagged custom identity | `3.1.17-custom`, gated on the real tag |
| Public latest/default | Keep `v3.1.16`, prior stable refs and `main` |
| Signed draft, evidence, new asset hashes | Not created |
| Support/default follow-up PRs | Separate reviewed stages; current links and gates belong in the pre-tag PR evidence |

The user authorized the six eligible merges and release/custom-build preparation.
PRs #217 and #209 remain excluded. Automatic CI from those changes is expected.
No actual tag, draft/release, manual workflow dispatch, service deployment or
device flash belongs to this stage. Do not change remote security settings.

| PR | Scope | Merge / Head Evidence At This Checkpoint |
| --- | --- | --- |
| [#276](https://github.com/decentespresso/openscale/pull/276) | Settled weight after impacts; stock CI artifact staging | Remote merge `227b947c44a93a6713ce74394d7d000e706cb67c` |
| [#277](https://github.com/decentespresso/openscale/pull/277) | Charger-status battery icon | Remote merge `1b5c3fbefe9356d803d79a1c710369bb7033d643` |
| [#283](https://github.com/decentespresso/openscale/pull/283) | Remove unused menu String comparison | Remote merge `3929896d388448b76bf4d983335f4e55699b9ee3` |
| [#284](https://github.com/decentespresso/openscale/pull/284) | Final custom binary identity and saved-build provenance | Remote merge `dcb53ac6687d8b7c4a6197a4aee5210e57160814`; corrected head `09952886993f5896648afa83922073f8e22cbae4` |
| [#285](https://github.com/decentespresso/openscale/pull/285) | App Text at 52 px/s with per-pixel deadlines | Remote merge `eb1f038823bf0d175b5e1493420c4eaaa66eee7f` |
| [#288](https://github.com/decentespresso/openscale/pull/288) | Held-menu XOR side strips | Remote merge `d6be37b8c3a3005f24056c1b4b1e26c0c7093534` |

The operator checked green heads before all six remote merges, with no bypass.
The reconciled #284 passed combined Python/native, standard/grinder/LittleFS,
Energy and minimal-custom CI in runs `44732476358`, `44732476401` and
`44732476345`. Those results retain their original source identities. They do
not establish final pre-tag CI or a signed 3.1.17 artifact.

## Draft Release Notes

- Preserve settled readings after bean impacts while retaining the existing
  drift-compensation limits.
- Show battery level when USB is present and charging is inactive, including
  the charging display, without changing USB standby controls.
- Scroll long App Text at 52 pixels per second while retaining short-text,
  session and higher-priority screen behavior.
- Invert side strips while holding Circle or Square in menu frames.
- Verify custom firmware version/source identity after `buildfs` and preserve
  each saved build's original source ref and commit.

## Documentation Audit

The release diff determines which prose needs changes. Display notes now cover
the shared charger-status icon and post-content menu overlay while retaining
#285's scroll deadlines. Build notes and the README distinguish the candidate
identity from the still-published 3.1.16 release and service evidence.

#276 supplies the settled-weight firmware guidance; #285 supplies protocol and
browser-example timing guidance. #284 supplies the build/release/operations
Version Identity Gate. Preserve those merged notes and the #286 status-recovery
and #287 authenticated-lookup guidance. No unrelated GPIO, storage, OTA, agent
rule or historical 3.1.16 record rewrite belongs to this preparation.

## Custom-Build Staging

Advance the source base and main metadata before freezing the candidate. Keep
support for nonexistent `v3.1.17` out of live main: the builder validates each
supported ref, so an absent tag can break existing builds. Prepare a separate
support PR, create the approved real tag later, then merge/regenerate against it.
Retain old refs, plugin compatibility, immutable combinations and recovery assets.

Real hosted main and tagged builds remain a later Version Identity Gate. Verify
source/builder commits, `3.1.17-main-custom` / `3.1.17-custom`, final binary and
ZIP hashes, production signatures, combination identity and live saved-build
provenance. Catalog/unit checks alone do not prove those hosted artifacts.

## Public-Default Activation

Keep the public selector/default at `v3.1.16`. Prepare activation as a separate
reviewed PR, gated on publication and service readiness. Later verify the live
options/default after Pages deployment. The source version bump does not prove
a Worker deployment, hosted build or public-default activation occurred.

## Validation Evidence

Record exact candidate SHAs, run IDs and hashes in the pre-tag PR/local report
when available. Reuse applicable completed evidence; identify the changed input
or uncovered risk before rerunning a check.

| Check | Evidence Gate |
| --- | --- |
| Six-PR scope and exclusions; full raw diff and whitespace | Review completed integration; attach final pre-tag evidence |
| AI documentation contract | Passed locally after prose and current-main identity inputs were prepared; recheck the final reviewed diff |
| Focused drift, charger-icon, scrolling and menu contracts | 21 focused checks passed on the six-PR tree with the 3.1.17 version bump; raw logs retained locally |
| Current-main custom identity and historical refs | 35 Worker/configurator tests, plugin catalog and the identity matrix passed; public default remains v3.1.16 |
| Python/Worker contracts and native tests | Successful final-head automatic CI pending |
| Standard, grinder, LittleFS, energy/regression jobs and `release-ready` | Successful final-head automatic CI pending |
| Minimal custom and affected plugin profiles | Final-head CI and final-after-buildfs identity evidence pending |
| Signed draft/eight assets and evidence digest | Not created; later authorized preparation |
| Hosted main/tagged artifacts and saved-build UI | Later authorized service gate |
| Device acceptance and production older-to-newer OTA | Not established for this candidate |

Remote builds can replace local IDF rebuilding. Do not weaken the pinned 80 MHz
configuration to avoid a local SDK compile. Workflow inspection or contracts
without successful build jobs do not establish firmware readiness. CI cannot
establish physical charging, weighing or OLED behavior.

### Configuration Names

On 2026-11-20 the operator checked names with host credentials. Repository
secrets `HDS_OTA_SIGNING_KEY_PEM`, `HDS_CUSTOM_OTA_SIGNING_KEY_PEM` and
`CUSTOM_BUILD_UPLOAD_TOKEN` are present, as is repository variable
`HDS_OTA_MANIFEST_PUBLIC_KEY_PEM`. Optional `HDS_RELEASE_MIN_FROM` is absent;
the workflow retains its default. The `firmware-release` environment has no
environment-specific secrets and inherits repository secrets.

No values were requested or exposed. Names do not prove key/signature agreement;
authorized preparation must check that. Production Worker/App credentials and
hosted old/new builds remain separate gates. Keep official Key 1 signing and
the separate custom-key policy.

## Remaining Release Gates

- Approve the exact main candidate after the pre-tag PR and relevant checks;
  accept or resolve material hardware limits before tagging.
- Keep `previous_tag=v3.1.16` and the runtime latest-stable check. Later signed
  preparation must verify the previous catalog, canonical URLs, compatibility,
  partition limits, hashes/sizes, required LittleFS and forward recovery.
- The later draft needs `HDS_FW_v3.1.17.zip`, `firmware.bin`, `littlefs.bin`,
  `dependencies.txt`, `manifest.json`, `manifest.sig`, `release-evidence.json`
  and `release-evidence.sig`. The USB ZIP contains exactly the four matching
  firmware, bootloader, partitions and LittleFS images.
- Publication requires separate approval of evidence from a successful
  preparation run, without rebuilding. Production OTA, custom-service rollout
  and default activation remain subsequent release work.

### Physical Qualification Limits

- #276: impacts, hot cups, slow pours and high Drift settings still need device
  qualification. A reported fresh-tare 100 g check read 99.3-99.4 g before the
  output filter; reviewers did not establish a regression. Host cases do not
  qualify physical accuracy or slow-loading behavior with Drift enabled.
- #277: USB-present/charger-inactive visual and serial evidence exists, but full
  protection stop/resume, PCB 8.3.1 and gauge variants remain unqualified.
- #285: a SPI/Chrome visual check does not measure 52 px/s or qualify measurement
  cadence, energy/sleep and rotation behavior during active scrolling.
- #288: physical OLED validation remains open.

Keep older hardware evidence under its original SHA. No exact-candidate USB
acceptance or production 3.1.16-to-3.1.17 pull OTA result exists yet. Production
firmware cannot pull draft-only assets, so that real update remains a
post-publication check or an accepted limitation, not a claimed pre-tag pass.
