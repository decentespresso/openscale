# Phase 1: Plugin Package and Catalog

## Goal

The existing asset catalog becomes the single trusted source for approved plugin packages. A package may remain asset-only or additionally provide an approved patch for specific firmware revisions.

## Package Structure

```text
plugins/<plugin-id>/
|-- plugin.json
|-- patches/
|   |-- main.patch
|   `-- v3.2.0.patch
`-- assets/
```

The `patches` and `assets` directories are optional. The existing `hello-web` package remains a valid asset-only plugin without a patch.

## Manifest

The manifest receives a new schema version and can map patch targets to an allowed firmware ref. During a build, every ref is resolved to an exact commit and recorded in the build manifest.

Required information:

- stable plugin ID and plugin version
- compatible firmware refs
- feature dependencies
- explicit plugin conflicts
- optional patch file for each firmware ref
- optional LittleFS assets
- declared flash, RAM, and LittleFS budgets

## Implementation

1. Extend the existing manifest parser with the new schema version and optional patch targets.
2. Strictly validate IDs, relative paths, file existence, and target refs.
3. Reject absolute paths, paths escaping the package, and duplicate asset targets.
4. Generate the browser catalog from validated manifests instead of maintaining dependencies twice.
5. Keep only IDs and the requested firmware ref in `custom-build.json`.
6. Extend the existing small catalog test with patch and failure cases.

## Security Boundary

A build request must never provide patch contents, a URL, or an external repository. Inputs select only previously approved IDs from the catalog in the trusted workflow checkout.

## Acceptance Criteria

- `hello-web` continues to work unchanged as an asset-only plugin.
- A test manifest with a valid version-specific patch is accepted.
- Unknown IDs, unknown firmware refs, and unsafe paths are rejected.
- The catalog and plugin manifests cannot silently expose different dependencies.
- Empty feature and plugin lists remain valid.

## Not Included

- patch application
- public build endpoint
- durable artifact storage
- custom OTA signing
