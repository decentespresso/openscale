# Phase 4: Public Build-on-Demand Service

## Goal

For a selected configuration, the existing GitHub Pages configurator either shows available downloads or requests exactly one new build. Secrets, validation, and rate limits remain outside the browser.

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
5. Enforce per-client and global limits plus a small maximum build concurrency.
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

## Operations and Security

- GitHub Pages hosts static files only.
- The Worker is the only public write boundary.
- The workflow and plugin catalog come from a fixed trusted branch or commit.
- Rate limits are enforced server-side; browser checks are not security controls.
- On abuse, first reduce global build concurrency. Add CAPTCHA or stronger identity checks only after demonstrated need.
- Logs include a request ID and combination hash, but retain IP addresses no longer than required for short-lived rate limiting.

## Pull OTA Behavior

Selecting `pull-ota` includes the existing OLED/button client. It remains pointed at the official signed release channel. An official update replaces the custom build and its plugin selection. The public build service signs no custom OTA manifests.

## Acceptance Criteria

- An existing combination can be downloaded without a new workflow run.
- A missing combination can be requested once and followed through completion.
- Unknown IDs, manipulated hashes, and disallowed firmware refs are rejected.
- GitHub and storage credentials are absent from delivered browser code.
- Rate limiting and global concurrency prevent unlimited builds.
- GitHub Pages, the standard build, the custom build, and the existing Stable Pull OTA contract remain functional.

## Not Included

- arbitrary external plugin repositories
- uploaded ZIP or patch files
- automatic patch conflict resolution
- dedicated signed Pull OTA channel for custom combinations
