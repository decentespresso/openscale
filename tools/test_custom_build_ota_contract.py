from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(path, value):
    text = (ROOT / path).read_text(encoding="utf-8")
    assert value in text, f"{path} missing {value}"
    return text


def main():
    header = require("include/custom_build_ota.h", 'HDS_CUSTOM_BUILD_NVS_NAMESPACE[] = "ota_custom"')
    require("include/custom_build_ota.h", "customBuildRandomHex(newDeviceId, 16)")
    require("include/custom_build_ota.h", "customBuildRandomHex(newDeviceSecret, 32)")
    require("include/custom_build_ota.h", "pullOtaVerifyManifestSignatureWithKeys")
    require("include/custom_build_ota.h", "HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_1_PEM")
    require("include/custom_build_ota.h", "HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_2_PEM")
    require("include/custom_build_ota.h", 'pullOtaDraw("Pair code", pairCode, "Valid 12 hours")')
    require("include/custom_build_ota.h", 'String(HDS_CUSTOM_BUILD_SERVICE_URL) + "/v1/" + combinationHash')
    require("include/custom_build_ota.h", "customBuildFetchManifest(rollbackCombinationHash, rollbackManifest)")
    require("include/custom_build_ota.h", "assignment.combinationHash,")
    require("include/custom_build_ota.h", "rollbackCombinationHash);")
    pull_ota = require("include/pull_ota.h", 'preferences.putString("combo", combinationHash) == 64')
    assert 'preferences.putString("rb_combo", rollbackCombinationHash) == 64' in pull_ota
    assert "pullOtaIdentityMatches(loaded.version, loaded.combinationHash)" in pull_ota
    assert "loaded.rollbackVersion, loaded.rollbackCombinationHash" in pull_ota
    assert "loaded.asset.url, loaded.combinationHash" in pull_ota
    assert "loaded.rollbackAsset.url, loaded.rollbackCombinationHash" in pull_ota
    assert "fleet_secret" not in header
    require("include/menu.h", '"Custom Build", customBuildMenu')
    require("src/hds.ino", '#include "custom_build_ota.h"')
    require(".github/workflows/custom-build.yml", "HDS_CUSTOM_OTA_SIGNING_KEY_PEM")
    release = require(".github/workflows/release.yml", "python tools/write_custom_ota_public_key_header.py")
    assert "HDS_CUSTOM_OTA_SIGNING_KEY_PEM" not in release
    print("custom build OTA contract tests passed")


if __name__ == "__main__":
    main()
