import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
CONFIG = ROOT / "include" / "config.h"
BUILD_METADATA = ROOT / "git_rev_macro.py"


def assert_contains(text, needle):
    if needle not in text:
        raise AssertionError(f"release workflow missing {needle}")


def assert_not_contains(text, needle):
    if needle in text:
        raise AssertionError(f"release workflow still contains {needle}")


def assert_before(text, left, right):
    left_index = text.find(left)
    right_index = text.find(right)
    if left_index < 0 or right_index < 0 or left_index >= right_index:
        raise AssertionError(f"expected {left} before {right}")


def main():
    config = CONFIG.read_text(encoding="utf-8")
    text = WORKFLOW.read_text(encoding="utf-8")
    assert_contains(text, 'TAG: ${{ github.event.inputs.tag }}')
    assert_contains(text, '[[ ! "$TAG" =~ ^v?[0-9]+\\.[0-9]+\\.[0-9]+$ ]]')
    assert_contains(text, 'git rev-parse --verify --end-of-options "refs/tags/$TAG^{commit}"')
    assert_contains(text, 'git checkout --detach "$TAG_COMMIT"')
    assert_contains(text, "tag $TAG predates the three-key OTA migration")
    assert_contains(text, "python tools/write_ota_public_key_header.py")
    assert_contains(text, 'echo "HDS_FIRMWARE_VERSION=${TAG#v}" >> "$GITHUB_ENV"')
    assert_contains(text, "HDS_OTA_SIGNING_KEY_PEM secret is required")
    assert_contains(text, "keys/ota/hds_ota_manifest_public_key_{1..3}.pem")
    assert_contains(text, "gh release list")
    assert_contains(text, "gh release download")
    assert_contains(text, "previous-release/manifest.json")
    assert_contains(text, "previous-release/manifest.sig")
    assert_contains(text, "openssl dgst -sha256 -verify")
    assert_contains(text, "--previous-manifest previous-release/manifest.json")
    assert_contains(text, "--catalog-min-version v3.1.13")
    assert_contains(text, 'grep -aFq "FW: $HDS_FIRMWARE_VERSION" .pio.nosync/build/esp32s3/firmware.bin')
    assert_contains(text, "python tools/generate_release_manifest.py --tag \"$HDS_FIRMWARE_VERSION\" --output-dir release-files --catalog --catalog-min-version v3.1.13")
    assert_contains(text, "gh release create")
    assert_contains(text, "--draft")
    assert_contains(text, "gh release upload")
    assert_contains(text, "release-files/littlefs.bin")
    assert_contains(text, "release-files/manifest.sig")
    assert_contains(text, "release-files/manifest.json")
    assert_contains(text, "verifyManifestSignature previous-release/manifest.json previous-release/manifest.sig")
    assert_contains(text, "verifyManifestSignature release-files/manifest.json release-files/manifest.sig")
    assert_contains(text, "gh release edit")
    assert_contains(text, "--draft=false")
    assert_before(text, "openssl dgst -sha256 -verify", "python tools/generate_release_manifest.py")
    assert_before(text, "tag $TAG predates the three-key OTA migration", "python tools/write_ota_public_key_header.py")
    assert_before(text, "python tools/write_ota_public_key_header.py", "verifyManifestSignature previous-release/manifest.json")
    assert_before(text, 'echo "HDS_FIRMWARE_VERSION=${TAG#v}"', "pio run -e esp32s3")
    assert_before(text, 'grep -aFq "FW: $HDS_FIRMWARE_VERSION"', "python tools/generate_release_manifest.py")
    assert_before(text, "python tools/generate_release_manifest.py", "verifyManifestSignature release-files/manifest.json")
    assert_before(text, "verifyManifestSignature release-files/manifest.json", "gh release create")
    assert_before(text, "release-files/manifest.sig", "release-files/manifest.json")
    assert_before(text, "release-files/manifest.json", "gh release edit")
    assert_not_contains(text, "git rev-parse ${{ github.event.inputs.tag }}")
    assert_not_contains(text, "gh release create ${{ github.event.inputs.tag }}")
    assert_not_contains(text, '--tag "${{ github.event.inputs.tag }}"')
    assert_not_contains(text, "HDS_OTA_MANIFEST_PUBLIC_KEY_PEM")
    assert_not_contains(text, "ota_public.pem")
    assert_not_contains(text, "littlefs_required")
    assert_not_contains(text, "LITTLEFS_REQUIRED")
    assert_not_contains(text, "HDS_LITTLEFS_REQUIRED")
    assert_not_contains(text, "test_catalog_" + "min_version")
    assert_not_contains(text, "TEST_CATALOG_" + "MIN_VERSION")

    assert_contains(config, '#define LINE1 (char*)"FW: " HDS_FIRMWARE_VERSION')
    environment = {**os.environ, "HDS_FIRMWARE_VERSION": "9.8.7"}
    result = subprocess.run(
        [sys.executable, str(BUILD_METADATA)],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or '-DHDS_FIRMWARE_VERSION="9.8.7"' not in result.stdout:
        raise AssertionError("build metadata did not emit the firmware version macro")
    environment["HDS_FIRMWARE_VERSION"] = "v9.8.7"
    result = subprocess.run(
        [sys.executable, str(BUILD_METADATA)],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        raise AssertionError("build metadata accepted an unnormalized firmware version")

    print("release workflow contract tests passed")

if __name__ == "__main__":
    main()
