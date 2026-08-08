# Phase 3: Reproducible Artifact Cache

## Goal

Identical combinations are built only once and receive durable publicly readable download URLs. GitHub Actions artifacts remain diagnostic outputs rather than end-user storage.

## Combination Hash

The cache key is SHA-256 over a canonical representation of:

- build contract schema version
- resolved firmware commit
- sorted feature list
- plugin IDs and versions
- patch and asset hashes
- PlatformIO environment and partition schema

A moving name such as `main` alone must not be part of the identity. The resolved commit is authoritative.

## Artifacts

A successful combination publishes at least these files under its hash:

```text
<combination-hash>/
|-- firmware.bin
|-- littlefs.bin
|-- bootloader.bin
|-- partitions.bin
|-- build-manifest.json
`-- dependencies.txt
```

## Implementation

1. Generate the canonical input and combination hash in the existing Python tool.
2. Calculate the hash before building and use it in the workflow, artifact name, and build manifest.
3. Before compilation, check whether a complete successful entry already exists.
4. On a hit, return the existing download metadata without rebuilding.
5. Publish successful files atomically under the hash.
6. Never mark failed or incomplete uploads as cache hits.
7. Define retention and deletion rules for old base commits that are no longer referenced.

## Storage Decision

Cloudflare R2 is the preferred storage for the first public deployment because files can be downloaded without a GitHub login and the same account can later host the Worker. Before implementation, confirm the current free allowance, maximum object size, and deletion rules. GitHub Release assets remain a possible manual intermediate solution, but no additional storage adapter is built speculatively.

## Acceptance Criteria

- Two identical requests resolve to the same hash and trigger at most one build.
- A change to the base commit, patch, or asset produces a new hash.
- Downloads work without a GitHub login.
- A build becomes visible as `ready` only after the upload is complete.
- The matching build manifest is available for every binary.
- Custom artifacts cannot be confused with official OTA releases or their signatures.

## Not Included

- public write API
- browser-triggered builds
- custom OTA signing
