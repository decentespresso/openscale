# Phase 4: Public Build-on-Demand Service

## Goal

For a selected configuration, the existing GitHub Pages configurator either shows available downloads or requests exactly one new build. Secrets, validation, and rate limits remain outside the browser.

## Cost Boundary

The Cloudflare Workers Free plan currently includes 100,000 requests per day with 10 milliseconds of CPU time per invocation. A paid Worker is therefore not required for the initial service. The Workers Paid plan is only needed when measured usage or CPU time exceeds the free limits and currently starts at $5 per month. Source checked on 2026-08-08: [Workers pricing](https://developers.cloudflare.com/workers/platform/pricing/).

## Hosting

The configurator is `docs/custom-build/index.html`. GitHub Pages serves `main` and `/docs` at `https://decentespresso.github.io/openscale/custom-build/`. No custom DNS domain is required.

The Cloudflare Worker uses `openscale-custom-builds.odevstudio.workers.dev` for the API and artifact downloads. The R2 bucket remains private and has no `r2.dev` endpoint.

## Implementation Status

The Phase 4 API, Durable Object coordinator, trusted service catalog, exact source-commit workflow inputs, authenticated build status callbacks, and configurator integration are deployed. GitHub Pages and the Worker are live, and a production BLE/USB-only request completed successfully. The temporary local GitHub App private-key file was deleted after that test.

The GitHub App installation currently covers the organization because the installer is not the organization owner. Restricting it to `decentespresso/openscale` remains the target least-privilege configuration. The Worker still fixes dispatches to `decentespresso/openscale`; the wider installation is documented security hardening, not a functional deployment blocker.

The initial server limits are three new builds per client per day, twenty new builds globally per day, and two attempts per combination. Duplicate requests do not consume another build slot. Client rate-limit hashes expire after 24 hours, while GitHub controls runner concurrency.

Each attempt has a two-hour lease and a UUID fencing token. The Durable Object alarm marks an expired `queued` or `building` attempt as failed, and late callbacks from an older attempt are rejected. Status responses include `attempts`, `max_attempts`, and `retryable`; a build request after the second failure returns HTTP 409.

## Required Secrets

The Worker requires `RATE_LIMIT_SALT`, `GITHUB_APP_ID`, `GITHUB_APP_INSTALLATION_ID`, and `GITHUB_APP_PRIVATE_KEY_PKCS8`. The private key value is the GitHub App key converted to unencrypted PKCS#8 DER and base64 encoded. The GitHub App receives Actions write access and should be restricted to `decentespresso/openscale`. Existing `UPLOAD_TOKEN` continues to authenticate artifact uploads and workflow status callbacks.

## Components

```text
GitHub Pages
    |
    v
Cloudflare Worker
    |-- validates catalog IDs and firmware ref
    |-- calculates or confirms combination hash
    |-- checks cache and rate limits
    `-- starts GitHub Actions when needed
             |
             v
        R2 artifacts
```

## Worker

1. Accept only canonical feature and plugin IDs.
2. Resolve and restrict the allowed firmware ref server-side.
3. Return status and downloads immediately for an existing cache entry.
4. Deduplicate concurrent requests for the same combination.
5. Enforce per-client and global daily limits.
6. Trigger GitHub with a minimally privileged server-side GitHub App or installation token.
7. Never expose GitHub credentials through the browser, logs, or artifacts.
8. Provide the simple states `missing`, `queued`, `building`, `ready`, and `failed`.

## Configurator

1. Continue to display selection and dependencies locally.
2. After selection, request the status of the canonical combination.
3. Show direct downloads for `ready`.
4. Show an explicit build button for `missing`.
5. After a request, poll status at a bounded interval.
6. Never start a potentially billable build merely because a selection changed.
7. Show concise errors without internal secrets.
8. Abort superseded requests and reject responses that no longer match the current selection.

## Compile Coverage Gate

CI proves the standard firmware, grinder environment, dependency resolution, patch application, hashing, cache contracts, and the representative custom matrix: no optional features; WiFi only; WiFi with the web server and no runtime LittleFS; WebSocket; ElegantOTA; Pull OTA without the web server; Grinder; and a plugin with assets. The full set of 62 valid core combinations remains unnecessary unless this matrix exposes an interaction failure.

## Operations and Security

- GitHub Pages hosts static files only.
- The Worker is the only public write boundary.
- The workflow and plugin catalog come from a fixed trusted branch or commit.
- Rate limits are enforced server-side; browser checks are not security controls.
- On abuse, first reduce global build concurrency. Add CAPTCHA or stronger identity checks only after demonstrated need.
- Logs include a request ID and combination hash, but retain IP addresses no longer than required for short-lived rate limiting.

## Pull OTA Behavior

Selecting `pull-ota` includes the existing OLED/button client. It remains pointed at the official signed release channel. An official update replaces the custom build and its plugin selection. The public build service signs no custom OTA manifests.

## Production Validation

Production validation completed on 2026-08-11:

- GitHub Pages and the Worker returned successful responses from the allowed origin, while an unrelated origin was rejected.
- Unknown features and disallowed firmware refs were rejected before dispatch.
- A missing BLE/USB-only combination progressed through GitHub Actions to `ready`.
- Its public ZIP contained exactly `firmware.bin`, `bootloader.bin`, `partitions.bin`, and `littlefs.bin`, with no factory image or separate public BIN downloads.
- The completed entry remained publicly readable through the status and download endpoints.

## Acceptance Criteria

- An existing combination can be downloaded without a new workflow run.
- A missing combination can be requested once and followed through completion.
- Unknown IDs, manipulated hashes, and disallowed firmware refs are rejected.
- GitHub and storage credentials are absent from delivered browser code.
- Rate limiting prevents unlimited builds.
- GitHub Pages, the standard build, the custom build, and the existing Stable Pull OTA contract remain functional.

## Not Included

- arbitrary external plugin repositories
- uploaded ZIP or patch files
- automatic patch conflict resolution
- dedicated signed Pull OTA channel for custom combinations
