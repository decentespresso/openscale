# Phase 2: Secure Patch Application and Build

## Goal

Selected approved patches are applied reproducibly to an exact firmware commit without modifying the normal checkout or standard build.

## Build Flow

1. The workflow starts from its trusted commit and reads only that commit's plugin catalog.
2. The requested allowed firmware ref is resolved to a commit SHA.
3. A clean temporary checkout of that commit is created for the build.
4. Dependencies and declared conflicts are checked before patch application.
5. Plugin dependencies are processed before their dependents; independent plugins use lexicographic plugin-ID order.
6. Each patch is first checked in the already patched temporary checkout with `git apply --check --whitespace=error`, then applied exactly.
7. Assets are copied to the LittleFS staging directory only after all patches apply successfully.
8. PlatformIO builds firmware and LittleFS non-interactively in the temporary checkout.

The trusted entry point is `python tools/build_custom_firmware.py`. It publishes a complete artifact directory only after both builds and provenance generation succeed.

## Conflict Behavior

- A declared conflict stops before patch application.
- A patch that does not apply stops the build.
- A combination that works only with a different patch order is considered incompatible.
- There is no `--3way`, automatic resolution, or patch modification during a build.

## Build Manifest

Every successful build records at least:

- resolved firmware commit
- canonical feature list
- plugin IDs and versions
- SHA-256 of every patch and asset
- PlatformIO environment and partition schema
- SHA-256 and size of every generated binary

The build manifest contains provenance metadata and is not an OTA signature.

## Tests

- asset plugin without a patch
- one valid patch
- two compatible patches
- declared conflict
- malformed or no longer applicable patch
- patch for the wrong firmware ref
- standard build without applied plugins
- BLE/USB build with an empty feature list

## Acceptance Criteria

- The normal working tree remains unchanged after both successful and failed builds.
- Standard and release builds use no plugin patches.
- Invalid combinations produce a clear diagnostic and no partial artifact.
- Repeated builds of the same input record the same source base and package hashes.
- Existing OTA and release contract tests remain green.

## Not Included

- durable cache storage
- anonymous build trigger
- custom OTA signing
