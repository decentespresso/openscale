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

1. Generate the canonical input and combination hash in the existing Python tool. Implemented locally.
2. Calculate the hash before building and use it in the workflow, artifact name, and build manifest. Implemented locally.
3. Before compilation, check whether a complete successful entry already exists. Implemented for complete local entries.
4. On a hit, return the existing download metadata without rebuilding. Implemented against the trusted Worker manifest endpoint.
5. Publish successful files atomically under the hash. Implemented locally.
6. Never mark failed or incomplete uploads as cache hits. Implemented locally.
7. Expire every `v1/` cache entry after 180 days. A later request rebuilds it under the same hash.

## Storage Decision

The production cache uses Cloudflare R2 Standard with these fixed settings:

- bucket: `openscale-custom-builds`
- bucket access: private
- download gateway: `openscale-custom-builds.odevstudio.workers.dev`
- object path: `v1/<combination-hash>/<filename>`
- public URL: `https://openscale-custom-builds.odevstudio.workers.dev/v1/<combination-hash>/<filename>`
- artifact retention: 180 days after upload
- browser cache: `Cache-Control: public, max-age=86400, immutable`
- browser CORS: `https://decentespresso.github.io`, `GET` and `HEAD` only
- public `r2.dev` access: disabled

No owned DNS zone is required for the first deployment. The read-only Worker streams approved objects from its private R2 binding and later hosts the Phase 4 API on the same `workers.dev` service. Only the custom-build workflow receives write and delete credentials. A custom domain can be attached later without changing object keys. This service is never used by the official signed Pull OTA channel.

The workflow uploads binaries and `dependencies.txt` first and uploads `build-manifest.json` last. The manifest is the commit marker: a combination is `ready` only when it exists and every recorded size and SHA-256 matches. A request after expiry is a normal cache miss and may rebuild the same combination.

As checked on 2026-08-08, the monthly free allowance is 10 GB-month of storage, 1 million Class A operations, 10 million Class B operations, and free egress. The verified six-file build entry is about 3.22 MB, so storage reaches the paid tier at roughly 3,000 retained combinations before operation limits become relevant. The 5 GiB single-part upload limit and 5 TiB object limit leave ample room for every build artifact.

Beyond the allowance, Standard storage is $0.015 per GB-month, Class A operations are $4.50 per million, and Class B operations are $0.36 per million. Standard storage has no minimum retention period. Deletes are free, lifecycle expiry normally removes an object within 24 hours, and billing stops after deletion.

The private Standard bucket is provisioned in Eastern Europe. Public `r2.dev` access is disabled, `v1/` expires after 180 days, and a $1 billing alert reports the first paid usage. The deployed Worker is bound to this bucket and returns the expected private-cache miss and CORS responses. Publication and downloads use the versioned contract in `cloudflare/custom-build-worker/`, with a 5 MiB limit per complete combination. The shared upload token is stored as the encrypted Worker secret `UPLOAD_TOKEN` and the GitHub Actions repository secret `CUSTOM_BUILD_UPLOAD_TOKEN`.

Sources: [R2 pricing](https://developers.cloudflare.com/r2/pricing/), [R2 limits](https://developers.cloudflare.com/r2/platform/limits/), [R2 public buckets](https://developers.cloudflare.com/r2/buckets/public-buckets/), and [R2 object lifecycles](https://developers.cloudflare.com/r2/buckets/object-lifecycles/). GitHub Release assets remain a possible manual fallback, but no additional storage adapter is built speculatively.

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
