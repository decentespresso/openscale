# 3.1.14 Release Checklist

This is an operator checklist, not evidence that the checks have passed.
Release preparation does not publish firmware or deploy the custom-build service.

## Administrator Setup

- Protect `main` with review and required CI, including `release-ready` and the
  candidate minimal custom build. Review bypass permissions.
- Restrict release-tag creation and prohibit replacement of published tags.
- Configure required reviewers for `firmware-release`, restrict it to `main`,
  and prevent self-approval where available. YAML alone does not configure this.
- Confirm the official signing secret matches a committed public key and has an
  offline backup. Record only its identifier, never private key material.
- Review artifact retention. Keep official and custom recovery assets available.

## Automated Preparation

- Record the approved candidate SHA, previous stable tag, supported hardware,
  optional-feature scope, and included changes.
- Create the immutable release tag only after explicit release authorization.
- Dispatch `Release firmware` from `main` with `tag`, `expected_commit`, and
  `previous_tag`. Use `v3.1.13` for the 3.1.14 baseline while it is still the most
  recently published stable release.
- Verify the candidate's Python/Node contracts, native tests, standard and energy
  firmware builds, and minimal custom firmware/filesystem build all succeed.
  Existing workflows are reused; no additional eight-profile matrix runs on PRs.
- Require successful previous-catalog downloads and signatures. A genuine empty
  history needs explicit `bootstrap_catalog=true` and an empty `previous_tag`;
  bootstrap never bypasses a failed download for an existing release.
- Record the successful preparation run, attempt, candidate SHA, and evidence
  SHA-256 shown after the uploaded draft has been downloaded and verified.
- Check the draft contains the USB ZIP, firmware, LittleFS, dependency inventory,
  manifest/signature, and evidence/signature. The ZIP has all four build images.

## Hardware And Service Sign-Off

- Test the exact final draft binaries by USB, recording their evidence digest.
  Check calibration/tare, buttons, BLE, WiFi, sleep/wake, charging, settings
  preservation, and each advertised optional feature on applicable hardware.
- Exercise changed OTA/recovery paths, including interrupted firmware and
  LittleFS writes, failed restore, minimal HTTP fallback, and menu repair.
  Verify no boot loop, no falsely reported success, and preserved credentials.
- Record the approved Worker, builder, and configurator/catalog revisions.
  Deploy service support before firmware relies on new fields. This workflow
  does not perform that deployment.
- Verify pairing, assignment, install progress/failure/completion, already-installed
  behavior, and service outages. Keep installed custom builds needed for recovery.
- Treat an RC USB test as separate from the production 3.1.13-to-3.1.14 OTA path.
  The unmodified stable picker cannot access an unpublished draft or an RC.

## Publish And Verify

- Review draft release notes and record hardware approval against the digest.
- Dispatch `Publish firmware` from `main` with the same tag and candidate SHA,
  plus the approved `evidence_sha256`. Approve its protected environment.
- Publication verifies the signed inventory, successful preparation run,
  unchanged tag, previous stable, and draft assets. It does not rebuild or sign.
- Confirm stable 3.1.14 becomes latest. Preview/RC releases must remain prerelease
  and must not replace latest.
- Immediately test the actual 3.1.13-to-3.1.14 production OTA transition and
  supported custom transitions. Record results and limitations.
- Establish ownership for post-release failures and a tested withdrawal process.
  Use a new version for corrected firmware rather than replacing published bytes.

A failed preparation may leave a draft. Leave it unpublished, investigate, and
explicitly review cleanup before retrying. Do not manually bypass its failed run.

Development-version naming and OTA picker behavior are unchanged by these
workflow changes. Ordinary local builds still use the source version; do not
mistake a local build's displayed version for proof that it is an official release.
