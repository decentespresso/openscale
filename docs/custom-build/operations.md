# Custom Build Operations

## Security Boundary

The GitHub App must be installed with **Only select repositories** and only
`decentespresso/openscale` selected. Its private key, installation ID, rate-limit salt,
and upload token belong in Cloudflare Worker secrets. Do not grant the App organization-wide
repository access.

The Worker rejects any `GITHUB_REPOSITORY` value other than
`decentespresso/openscale`. Installation tokens are additionally requested for only the
`openscale` repository and only the Actions write permission.

Custom builds are unsigned USB installation archives. They are not official signed OTA
releases and must not be presented as verified or signed builds.

## Deployment Order

1. Deploy the Worker first. It accepts requests without `catalog_revision`, legacy feature
   arrays, and plugin catalogs without `conflicts_features`.
2. Merge the regenerated service catalog, browser catalog, and configurator together.
3. Confirm that a status request with the published `catalog_revision` succeeds.
4. Treat sustained `409 catalog_stale` responses as an incomplete deployment. The browser
   retries one catalog reload and then displays `Configurator update in progress`.

## Blocking a Plugin

1. Stop new combinations by removing or fixing the plugin package on the trusted builder
   branch and regenerate both catalogs. Do not add an emergency public admin route.
2. Merge the change and confirm that the service catalog at the new builder commit no longer
   accepts the affected plugin or combination.
3. Collect every affected combination hash from incident records and each build manifest.
4. In the Cloudflare dashboard, delete `v1/<hash>/build-manifest.json` first. This immediately
   makes every public download for that hash return `404`.
5. Delete `v1/<hash>/HDS_FW_custom.zip` and `v1/<hash>/dependencies.txt` after the manifest.
6. Verify the archive, manifest, and status endpoints from an unauthenticated client.
7. Record the plugin version, firmware refs, hashes, reason, operator, and deletion time in the
   incident record.

If the upload token may be exposed, rotate `UPLOAD_TOKEN` and
`CUSTOM_BUILD_UPLOAD_TOKEN`. If the GitHub App key may be exposed, revoke the key, create a
new one, and update the Worker secret before re-enabling builds.

## Public Failure Codes

| Code | Meaning |
| --- | --- |
| `dispatch_failed` | The Worker could not start the GitHub Actions workflow. |
| `build_failed` | The workflow completed unsuccessfully. |
| `build_timeout` | A queued or running build exceeded its lease. |

Internal logs, exception text, workflow output, and credentials are never returned by the
public status API.
