import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import configparser
import re
import textwrap


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
CONFIG = ROOT / "include" / "config.h"
BUILD_METADATA = ROOT / "git_rev_macro.py"
NIGHTLY_WORKFLOW = ROOT / ".github" / "workflows" / "nightly.yml"
OTA_WORKFLOW = ROOT / ".github" / "workflows" / "ota-contracts.yml"
PLATFORMIO_CONFIG = ROOT / "platformio.ini"
PLATFORMIO_REQUIREMENTS = ROOT / "requirements-platformio.txt"


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
    nightly = NIGHTLY_WORKFLOW.read_text(encoding="utf-8")
    ota = OTA_WORKFLOW.read_text(encoding="utf-8")
    assert_contains(text, '[[ ! "$TAG" =~ ^v?[0-9]+\\.[0-9]+\\.[0-9]+(-(preview|rc)\\.[0-9]+)?$ ]]')
    assert_contains(text, 'RELEASE_FLAGS=(--prerelease --latest=false)')
    assert_contains(text, '"${RELEASE_FLAGS[@]}"')
    assert_contains(text, 'preview tag predates exact-version rollback lookup')
    assert_contains(text, "grep -Fxq '#define HDS_OTA_RELEASE_RECOVERY_VERSION 1' include/pull_ota_version.h")
    assert_not_contains(text, 'grep -Fq "pullOtaVersionIsRelease"')
    assert_contains(text, 'git rev-parse --verify --end-of-options "refs/tags/$TAG^{commit}"')
    assert_contains(text, 'git checkout --detach "$TAG_COMMIT"')
    assert_contains(text, "tag $TAG predates the three-key OTA migration")
    assert_contains(text, "tag $TAG predates the release version injection migration")
    assert_contains(text, 'grep -Fq "HDS_FIRMWARE_VERSION" git_rev_macro.py || ! grep -Fq "HDS_FIRMWARE_VERSION" include/config.h')
    assert_contains(text, "tag $TAG predates the dependency pin migration")
    assert_contains(text, '[ ! -f requirements-platformio.txt ]')
    assert_contains(text, "python tools/write_ota_public_key_header.py")
    assert_contains(text, "python tools/write_custom_ota_public_key_header.py")
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
    assert_contains(text, "python tools/generate_release_manifest.py --tag \"$TAG\" --output-dir release-files --catalog --catalog-min-version v3.1.13")
    assert_contains(text, "gh release create")
    assert_contains(text.split('gh release create "$TAG"', 1)[1].split('gh release upload', 1)[0], '--verify-tag')
    assert_contains(text, "--draft")
    assert_contains(text, "gh release upload")
    assert_contains(text, "release-files/littlefs.bin")
    assert_contains(text, "release-files/manifest.sig")
    assert_contains(text, "release-files/manifest.json")
    assert_contains(text, "verifyManifestSignature previous-release/manifest.json previous-release/manifest.sig")
    assert_contains(text, "release-files/dependencies.txt")
    assert_contains(text, "verifyManifestSignature release-files/manifest.json release-files/manifest.sig")
    assert_not_contains(text, "gh release edit")
    assert_not_contains(text, "--draft=false")
    assert_contains(text, 'gh release download "$TAG" --repo "$GITHUB_REPOSITORY" --dir downloaded-release')
    assert_before(text, 'gh release upload "$TAG" release-files/manifest.json', 'python tools/verify_release_assets.py')
    assert_before(text, 'python tools/verify_release_assets.py', 'python tools/release_publication.py evidence')
    assert_before(text, 'python tools/release_publication.py evidence', 'python tools/release_publication.py verify-draft')
    assert_before(text, "openssl dgst -sha256 -verify", "python tools/generate_release_manifest.py")
    assert_before(text, "tag $TAG predates the three-key OTA migration", "python tools/write_ota_public_key_header.py")
    assert_before(text, "tag $TAG predates the release version injection migration", "python tools/write_ota_public_key_header.py")
    assert_before(text, "tag $TAG predates the dependency pin migration", "python -m pip install --requirement requirements-platformio.txt")
    assert_before(text, "python tools/write_ota_public_key_header.py", "verifyManifestSignature previous-release/manifest.json")
    assert_before(text, 'echo "HDS_FIRMWARE_VERSION=${TAG#v}"', "pio run -e esp32s3")
    assert_before(text, 'grep -aFq "FW: $HDS_FIRMWARE_VERSION"', "python tools/generate_release_manifest.py")
    assert_before(text, "python tools/generate_release_manifest.py", "verifyManifestSignature release-files/manifest.json")
    assert_before(text, "verifyManifestSignature release-files/manifest.json", "gh release create")
    assert_before(text, "release-files/manifest.sig", "release-files/manifest.json")
    publish = (ROOT / ".github/workflows/publish-release.yml").read_text(encoding="utf-8")
    assert_contains(text, 'needs: [resolve, preflight, custom-preflight]')
    assert_contains(text, 'uses: ./.github/workflows/nightly.yml')
    assert_contains(text, 'uses: ./.github/workflows/custom-build.yml')
    assert text.count('commit: ${{ inputs.expected_commit }}') >= 2
    assert_contains(text, 'test "$TAG_COMMIT" = "$EXPECTED_COMMIT"')
    assert_contains(text, 'if [ "$PREVIOUS_TAG" != "$EXPECTED_PREVIOUS_TAG" ]')
    for workflow in (text, publish):
        for value in ('environment: firmware-release', 'group: firmware-release', 'cancel-in-progress: false',
                      'test "$GITHUB_REF" = refs/heads/main', 'persist-credentials: false'):
            assert_contains(workflow, value)
    assert_contains(publish, 'python tools/release_publication.py publish')
    assert_contains(publish, '--evidence-sha256 "$EVIDENCE_SHA256"')
    assert_not_contains(publish, 'pio run')
    assert_not_contains(publish, 'HDS_OTA_SIGNING_KEY_PEM')
    assert_contains(nightly, 'workflow_call:')
    assert nightly.count('ref: ${{ inputs.commit || github.sha }}') == 4
    assert_contains(nightly, 'needs: [ai-documentation, native-tests, build, energy-build]')
    assert_contains(nightly, 'if: always()')
    assert_contains(nightly, 'node --test cloudflare/custom-build-worker/test/*.test.mjs')
    gate = nightly.split('\n  release-ready:\n', 1)[1].split("python - <<'PY'\n", 1)[1]
    gate = textwrap.dedent(gate.rsplit('\n          PY', 1)[0])
    jobs = ('ai-documentation', 'native-tests', 'build', 'energy-build')
    for changedJob in jobs:
        for result in ('success', 'failure', 'cancelled', 'skipped'):
            results = {job: {'result': result if job == changedJob else 'success'} for job in jobs}
            checked = subprocess.run([sys.executable, '-c', gate], capture_output=True, text=True,
                                     env={**os.environ, 'RESULTS': json.dumps(results)})
            assert (checked.returncode == 0) == (result == 'success'), (changedJob, result, checked.stderr)
    assert_not_contains(text, "git rev-parse ${{ github.event.inputs.tag }}")
    assert_not_contains(text, "gh release create ${{ github.event.inputs.tag }}")
    assert_not_contains(text, '--tag "${{ github.event.inputs.tag }}"')
    assert_not_contains(text, "HDS_OTA_MANIFEST_PUBLIC_KEY_PEM")
    assert_not_contains(text, "HDS_CUSTOM_OTA_SIGNING_KEY_PEM")
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
    environment["HDS_FIRMWARE_VERSION"] = "3.1.14-custom"
    result = subprocess.run(
        [sys.executable, str(BUILD_METADATA)],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or '-DHDS_FIRMWARE_VERSION="3.1.14-custom"' not in result.stdout:
        raise AssertionError("build metadata did not accept the custom suffix")
    for invalidVersion in (
        "v9.8.7", "9.8.7-Preview.1", "9.8.7-preview..1", "9.8.7-preview/1",
        "9.8.7-.preview", "9.8.7-preview.",
    ):
        environment["HDS_FIRMWARE_VERSION"] = invalidVersion
        result = subprocess.run(
            [sys.executable, str(BUILD_METADATA)],
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            raise AssertionError(f"build metadata accepted invalid firmware version {invalidVersion}")

    with tempfile.TemporaryDirectory() as tempDir:
        root = Path(tempDir)
        buildDir = root / "build"
        outputDir = root / "release-files"
        buildDir.mkdir()
        (buildDir / "firmware.bin").write_bytes(b"firmware")
        (buildDir / "littlefs.bin").write_bytes(b"littlefs")
        environment = {
            key: value
            for key, value in os.environ.items()
            if key not in {"HDS_OTA_SIGNING_KEY_FILE", "HDS_OTA_SIGNING_KEY_PEM", "HDS_RELEASE_NOTES_URL"}
        }
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "generate_release_manifest.py"),
                "--tag",
                "v9.8.7",
                "--build-dir",
                str(buildDir),
                "--output-dir",
                str(outputDir),
                "--repository",
                "decentespresso/openscale",
            ],
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise AssertionError(result.stderr)
        manifest = json.loads((outputDir / "manifest.json").read_text(encoding="utf-8"))
        if manifest["version"] != "9.8.7":
            raise AssertionError("release workflow did not normalize manifest version")
        expectedReleaseUrl = "https://github.com/decentespresso/openscale/releases/tag/v9.8.7"
        if manifest["release_notes_url"] != expectedReleaseUrl:
            raise AssertionError("release workflow stripped v from release notes URL")
        expectedFirmwareUrl = "https://github.com/decentespresso/openscale/releases/download/v9.8.7/firmware.bin"
        if manifest["firmware"]["url"] != expectedFirmwareUrl:
            raise AssertionError("release workflow stripped v from firmware URL")

    for workflow in (text, nightly):
        assert_contains(workflow, "python -m pip install --requirement requirements-platformio.txt")
        assert_contains(workflow, "hashFiles('platformio.ini', 'requirements-platformio.txt')")
        assert_contains(workflow, "pio pkg list -e")
        assert_not_contains(workflow, "pip install --upgrade platformio")
    assert_contains(ota, '- "platformio.ini"')
    assert_contains(ota, '- "requirements-platformio.txt"')
    assert_not_contains(ota, "pio run -e")
    assert_not_contains(ota, "python -m pip install --requirement requirements-platformio.txt")
    snapshotArtifact = nightly.split("name: HDS-snapshot-", 1)[1].split("- name: Save dependency inventory", 1)[0]
    for filename in ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"):
        assert_contains(snapshotArtifact, filename)
    assert_not_contains(snapshotArtifact, "dependencies.txt")
    assert_contains(nightly, "name: HDS-dependencies-")
    assert_contains(nightly, ".pio.nosync/build/esp32s3/dependencies.txt")
    requirements = PLATFORMIO_REQUIREMENTS.read_text(encoding="utf-8").splitlines()
    if requirements != ["platformio==6.1.19"]:
        raise AssertionError("PlatformIO Core must be pinned exactly")
    config = configparser.ConfigParser(interpolation=None)
    config.read(PLATFORMIO_CONFIG, encoding="utf-8")
    dependencies = tuple(
        dependency.strip()
        for dependency in config["env:esp32s3"]["lib_deps"].splitlines()
        if dependency.strip()
    )
    registryPin = re.compile(r"[^/\s]+/.+ @ [0-9]+\.[0-9]+\.[0-9]+$")
    gitPin = re.compile(r"https://github\.com/.+\.git#[0-9a-f]{40}$")
    for dependency in dependencies:
        if registryPin.fullmatch(dependency) is None and gitPin.fullmatch(dependency) is None:
            raise AssertionError(f"dependency is not pinned exactly: {dependency}")

    print("release workflow contract tests passed")

if __name__ == "__main__":
    main()
