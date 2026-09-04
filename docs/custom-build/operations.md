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

## Fleet Data Model

The Durable Object stores fleet records under the SHA-256 hash of the recovery key. A fleet record
contains device IDs and immutable build references. Each build reference contains the full
`combination_hash`, a user-editable label, firmware version, feature and plugin identifiers, and the
time it was added. The binaries remain in R2 under `v1/<combination-hash>/`; adding or removing a
fleet reference never copies or deletes those objects.

Device records distinguish these fields:

- `desired_combination` is the full custom build hash fleet management wants the scale to install.
- `installed_combination` is the full custom build hash reported by the authenticated scale.
- `firmware_version` is the version reported by the authenticated scale.
- `last_seen_at` is updated only by an authenticated device check-in.
- `desired_updated_at` records the most recent assignment change for deployment status.

Assignment does not imply installation. The scale's compile-time
`HDS_CUSTOM_BUILD_COMBINATION_HASH` is authoritative for the firmware currently executing. Custom
firmware reports that value; official firmware reports `installed_combination: null`. Existing
records without the newer fields remain valid and appear as unknown until they check in.

Full 64-character lowercase hashes are used for API identity, storage, assignment, and verification.
The 12-character browser prefix and 8-character OLED prefix are uppercase display aids only.

## Fleet API

All fleet routes require the fleet recovery key. All device routes require the device ID and device
secret established during physical pairing.

| Method | Route | Purpose |
| --- | --- | --- |
| `POST` | `/api/v1/device/check-in` | Report installed hash and firmware version, then receive the current assignment. |
| `GET` | `/api/v1/fleet/overview` | Return scales, saved builds, and one canonical state per unique combination hash. |
| `GET` | `/api/v1/fleet/builds` | List saved build references. |
| `POST` | `/api/v1/fleet/builds` | Add one ready custom build by full combination hash. |
| `PATCH` | `/api/v1/fleet/builds/<hash>` | Rename a saved build reference. |
| `DELETE` | `/api/v1/fleet/builds/<hash>` | Remove an unassigned build reference. |
| `POST` | `/api/v1/fleet/assignments` | Assign or clear a build for selected scales or every scale. |

The check-in body is bounded to the two fields below. Its response stays limited to linking,
assignment, and canonical build state; it does not include fleet lists, descriptions, or asset URLs.

```json
{
  "installed_combination": null,
  "firmware_version": "3.1.14"
}
```

Selected bulk assignment uses `device_ids`; all-scale assignment uses `"all": true`. A null
`combination_hash` deliberately clears the assignment. Target IDs are deduplicated, every target is
validated before the transaction writes, and cross-fleet or missing targets fail the whole request.

Removing a saved build that is still desired by any scale returns `409 build_assigned`. Clear or
replace those assignments first. Removal leaves R2 artifacts and every scale's
`installed_combination` untouched.

## Scale Resource Contract

The scale performs one check-in only when the Custom Build menu is opened. It retains one desired
hash and the compile-time installed hash only for the lifetime of that operation. It does not fetch
the build library, cache fleet data, keep deployment history, add a polling loop, or add NVS fields.

After validating the full desired hash, firmware compares it with the normalized compile-time hash.
An exact match returns before manifest, signature, firmware, LittleFS, OTA-state, or reboot work. The
OLED shows `Already installed` and the 8-character hash prefix. The OTA task stack remains 24,576
bytes.

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
