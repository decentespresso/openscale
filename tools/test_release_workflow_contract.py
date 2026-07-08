from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"


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
    text = WORKFLOW.read_text(encoding="utf-8")
    assert_contains(text, 'TAG: ${{ github.event.inputs.tag }}')
    assert_contains(text, 'default: "true"')
    assert_contains(text, '[[ ! "$TAG" =~ ^v?[0-9]+\\.[0-9]+\\.[0-9]+$ ]]')
    assert_contains(text, 'git rev-parse --verify --end-of-options "refs/tags/$TAG^{commit}"')
    assert_contains(text, 'git checkout --detach "$TAG_COMMIT"')
    assert_contains(text, "python tools/write_ota_public_key_header.py")
    assert_contains(text, "HDS_OTA_SIGNING_KEY_PEM secret is required")
    assert_contains(text, "HDS_OTA_MANIFEST_PUBLIC_KEY_PEM variable is required")
    assert_contains(text, "gh release list")
    assert_contains(text, "gh release download")
    assert_contains(text, "previous-release/manifest.json")
    assert_contains(text, "previous-release/manifest.sig")
    assert_contains(text, "openssl dgst -sha256 -verify")
    assert_contains(text, "--previous-manifest previous-release/manifest.json")
    assert_contains(text, "--catalog-min-version v3.1.13")
    assert_contains(text, "python tools/generate_release_manifest.py --tag \"$TAG\" --output-dir release-files --catalog --catalog-min-version v3.1.13")
    assert_contains(text, "gh release create")
    assert_contains(text, "--draft")
    assert_contains(text, "gh release upload")
    assert_contains(text, "release-files/manifest.sig")
    assert_contains(text, "release-files/manifest.json")
    assert_contains(text, "gh release edit")
    assert_contains(text, "--draft=false")
    assert_before(text, "release-files/firmware.bin", "release-files/manifest.json")
    assert_before(text, "openssl dgst -sha256 -verify", "python tools/generate_release_manifest.py")
    assert_before(text, "release-files/manifest.sig", "release-files/manifest.json")
    assert_before(text, "release-files/manifest.json", "gh release edit")
    assert_not_contains(text, "git rev-parse ${{ github.event.inputs.tag }}")
    assert_not_contains(text, "gh release create ${{ github.event.inputs.tag }}")
    assert_not_contains(text, '--tag "${{ github.event.inputs.tag }}"')
    assert_not_contains(text, "test_catalog_" + "min_version")
    assert_not_contains(text, "TEST_CATALOG_" + "MIN_VERSION")
    print("release workflow contract tests passed")


if __name__ == "__main__":
    main()
