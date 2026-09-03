# Custom Build Operations

## Security Boundary

The GitHub App must be installed with **Only select repositories** and only
`decentespresso/openscale` selected. Its private key, installation ID, rate-limit salt,
and upload token belong in Cloudflare Worker secrets. Do not grant the App organization-wide
repository access.

The Worker rejects any `GITHUB_REPOSITORY` value other than
`decentespresso/openscale`. Installation tokens are additionally requested for only the
`openscale` repository and only the Actions write permission.

Custom OTA manifests use two key slots that are separate from the official release keys. Commit
both public keys under `keys/ota/`. Store the active Key 1 private key only as the
`HDS_CUSTOM_OTA_SIGNING_KEY_PEM` GitHub Actions secret and keep both private keys in an encrypted
offline backup. Release and custom firmware embed both public keys. Firmware compilation receives
only public-key files; the active private key is passed only to the manifest-signing process.
Never reuse an official release signing key for custom builds.

The Worker stores SHA-256 hashes of fleet and device secrets, never their raw values. Treat a fleet
recovery key as a bearer credential. A scale's `device_secret` stays in the `ota_custom` NVS
namespace and must never be displayed or logged.

The browser keeps the fleet recovery key only in tab-scoped session storage. It removes the legacy
persistent copy when the configurator next opens. Move the configurator to a dedicated origin before
making recovery keys persistent again.

## Deployment Order

1. Create two custom OTA RSA key pairs, commit both public keys, save the Key 1 private key as the
   `HDS_CUSTOM_OTA_SIGNING_KEY_PEM` repository secret, and secure both private keys offline.
2. Deploy the Worker before publishing firmware with the Custom Build menu.
3. Merge the firmware, workflow, catalogs, and configurator together.
4. Publish an official release that embeds the custom OTA public key.
5. Confirm that pairing, fleet recovery, build assignment, signed manifest verification, and
   rollback work on hardware before enabling scale installation in the configurator.
6. Confirm that a status request with the published `catalog_revision` succeeds.
7. Treat sustained `409 catalog_stale` responses as an incomplete deployment. The browser
   retries one catalog reload and then displays `Configurator update in progress`.

## Clearing Pre-Launch Builds

Builds live in the private `openscale-custom-builds` R2 bucket under
`v1/<combination-hash>/`. A full pre-launch cleanup does not require collecting hashes:
open **Cloudflare Dashboard > R2 > openscale-custom-builds > Objects > v1**, select all
objects, and delete them.

For a targeted cleanup, use the hash shown as the object's directory name. Delete the
manifest first so its downloads immediately return `404`, then delete the archive and
dependency inventory:

```powershell
npx wrangler r2 object delete openscale-custom-builds/v1/<hash>/build-manifest.json --remote --config cloudflare/custom-build-worker/wrangler.toml
npx wrangler r2 object delete openscale-custom-builds/v1/<hash>/HDS_FW_custom.zip --remote --config cloudflare/custom-build-worker/wrangler.toml
npx wrangler r2 object delete openscale-custom-builds/v1/<hash>/dependencies.txt --remote --config cloudflare/custom-build-worker/wrangler.toml
npx wrangler r2 object delete openscale-custom-builds/v1/<hash>/firmware.bin --remote --config cloudflare/custom-build-worker/wrangler.toml
npx wrangler r2 object delete openscale-custom-builds/v1/<hash>/littlefs.bin --remote --config cloudflare/custom-build-worker/wrangler.toml
npx wrangler r2 object delete openscale-custom-builds/v1/<hash>/ota-manifest.sig --remote --config cloudflare/custom-build-worker/wrangler.toml
npx wrangler r2 object delete openscale-custom-builds/v1/<hash>/ota-manifest.json --remote --config cloudflare/custom-build-worker/wrangler.toml
```

R2 deletion does not reset weekly rate-limit counters; the next request for that combination
is a new build and consumes a build slot.

## Blocking a Plugin

1. Stop new combinations by removing or fixing the plugin package on the trusted builder
   branch and regenerate both catalogs. Do not add an emergency public admin route.
2. Merge the change and confirm that the service catalog at the new builder commit no longer
   accepts the affected plugin or combination.
3. Collect every affected combination hash from incident records and each build manifest.
4. In the Cloudflare dashboard, delete `v1/<hash>/build-manifest.json` first. This immediately
   makes every public download for that hash return `404`.
5. Delete the archive, dependency inventory, OTA binaries, and signed OTA manifest after the
   build manifest.
6. Verify the archive, manifest, and status endpoints from an unauthenticated client.
7. Record the plugin version, firmware refs, hashes, reason, operator, and deletion time in the
   incident record.

If the upload token may be exposed, rotate `UPLOAD_TOKEN` and
`CUSTOM_BUILD_UPLOAD_TOKEN`. If the GitHub App key may be exposed, revoke the key, create a
new one, and update the Worker secret before re-enabling builds.

If the active custom OTA signing key is compromised, disable scale installation, replace the
repository secret with the reserved Key 2 private key, increment
`CUSTOM_OTA_SIGNING_KEY_GENERATION`, and only then resume custom OTA builds. The generation change
invalidates cached combinations signed with Key 1. Publish official firmware with a new reserve
public key before retiring a compromised public key. A lost but uncompromised key must remain
trusted until existing firmware has migrated to its replacement.

## Public Failure Codes

| Code | Meaning |
| --- | --- |
| `dispatch_failed` | The Worker could not start the GitHub Actions workflow. |
| `build_failed` | The workflow completed unsuccessfully. |
| `build_timeout` | A queued or running build exceeded its lease. |

Internal logs, exception text, workflow output, and credentials are never returned by the
public status API.
