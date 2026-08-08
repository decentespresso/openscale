# Master Plan: Approved Plugin Builds

## Goal

OpenScale should build reproducible custom firmware from an unchanged firmware revision, compile-time features, and approved plugin packages. Plugins may contain version-specific patch files and LittleFS assets, but they are applied only in a temporary build checkout. The normal firmware source tree contains no activated plugin code.

The existing configurator will later become the shared interface for existing downloads and new build requests. Unknown plugin sources, uploaded ZIP files, and arbitrary patch URLs remain excluded.

## Current State

- Compile-time features are resolved for `esp32s3-custom`.
- An empty feature list produces BLE/USB firmware without WiFi.
- Pull OTA, ElegantOTA, and the web server are separate features.
- Pull OTA requires WiFi, but neither the web server nor the runtime `littlefs` feature.
- The existing Pull OTA flow still installs the mandatory `firmware.bin` and `littlefs.bin` assets to separate partitions.
- `hello-web` is an approved asset plugin without a firmware patch.
- The configurator currently generates only a maintainer command for `custom-build.yml`.
- The custom workflow resolves an allowed firmware ref to an exact commit and applies approved patches only in a temporary checkout.
- Custom artifacts use a canonical combination hash and a complete local cache entry. Actions artifacts remain time-limited.
- The private R2 Standard cache and read gateway are provisioned at `openscale-custom-builds.odevstudio.workers.dev`, with 180-day artifact retention. The Worker and GitHub Actions share an encrypted upload token.

## Invariants

- The normal `esp32s3` build and release workflow keep their current behavior.
- Stable Pull OTA remains controlled through the OLED and buttons and installs only officially signed releases.
- A custom build with Pull OTA currently updates from the official release channel and loses its custom selection when updated.
- Production signing keys are never provided to a public custom-build service.
- Initial custom artifacts are intended for USB installation, not for a separate signed Pull OTA channel.
- The build service accepts only plugin and feature IDs from the validated catalog in the trusted workflow commit.
- Patch application is exact. There is no automatic three-way merge or conflict resolution.

## Phases

1. [Phase 1: Plugin Package and Catalog](plan_1_phase.md)
2. [Phase 2: Secure Patch Application and Build](plan_2_phase.md)
3. [Phase 3: Reproducible Artifact Cache](plan_3_phase.md)
4. [Phase 4: Public Build-on-Demand Service](plan_4_phase.md)

Each phase must be independently deliverable. The next phase starts only after the previous phase meets its acceptance criteria.

## Target Flow

```text
GitHub Pages configurator
        |
        | feature and plugin IDs
        v
Build API validates and creates combination hash
        |
        +-- cache hit --> public downloads
        |
        +-- cache miss --> GitHub Actions
                                |
                                | check out exact firmware commit
                                | validate and apply approved patches
                                | build firmware and LittleFS
                                v
                         store artifacts and status
```

## Shared Definition of Done

- The same base commit and package contents produce the same combination hash.
- No custom build modifies the normal source checkout.
- Every build records the base commit, features, plugin versions, and content hashes.
- Invalid IDs, paths, dependencies, conflicts, and patch failures stop before compilation.
- The standard build, native tests, OTA contract tests, and release contract tests remain green.
- Secrets are present in neither GitHub Pages nor downloadable build artifacts.

## Deliberately Deferred

A dedicated Pull OTA channel for custom combinations is not part of these four phases. It would require a separate signing key pair, profile-specific manifest URLs, a mandatory combination hash in the OTA manifest, and signed `firmware.bin` and `littlefs.bin` assets. That extension receives its own plan only after build-on-demand operation is stable.
