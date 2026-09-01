#!/usr/bin/env python3
import argparse
import json
import os
import tempfile
import urllib.parse
from pathlib import Path

import configure_custom_build as customBuild
import generate_release_manifest as releaseManifest


def requireBaseUrl(value):
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError("custom OTA base URL must be HTTPS without credentials")
    if parsed.query or parsed.fragment:
        raise ValueError("custom OTA base URL cannot contain a query or fragment")
    return value.rstrip("/")


def customManifest(buildDir, build, baseUrl):
    combinationHash = build.get("combination_hash", "")
    if len(combinationHash) != 64 or any(character not in "0123456789abcdef" for character in combinationHash):
        raise ValueError("build manifest has an invalid combination hash")
    assetRoot = f"{requireBaseUrl(baseUrl)}/v1/{combinationHash}"
    asset = lambda name: {
        "url": f"{assetRoot}/{name}",
        "size": (buildDir / name).stat().st_size,
        "sha256": releaseManifest.sha256_file(buildDir / name),
    }
    littlefs = asset("littlefs.bin")
    littlefs["required"] = True
    return {
        "model": "hds",
        "version": build["firmware_version"],
        "min_from": "3.1.13",
        "release_notes_url": "https://decentespresso.github.io/openscale/custom-build/",
        "chip": "esp32s3",
        "environment": "esp32s3",
        "flash_size": 8388608,
        "partition_schema": "esp32s3-default-8mb-ota-spiffs-1536k",
        "app_partition_min_size": 3342336,
        "fs_partition_label": "spiffs",
        "fs_partition_size": 1572864,
        "fs_schema": 1,
        "firmware": asset("firmware.bin"),
        "littlefs": littlefs,
        "custom_build": True,
        "combination_hash": combinationHash,
    }


def writeArtifacts(buildDir, buildManifestPath, baseUrl, signingKey):
    if not signingKey:
        raise ValueError("HDS_CUSTOM_OTA_SIGNING_KEY_PEM is required")
    build = json.loads(buildManifestPath.read_text(encoding="utf-8"))
    manifestPath = buildDir / "ota-manifest.json"
    signaturePath = buildDir / "ota-manifest.sig"
    releaseManifest.write_manifest(customManifest(buildDir, build, baseUrl), manifestPath)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as handle:
        handle.write(signingKey)
        signingKeyPath = Path(handle.name)
    try:
        releaseManifest.sign_manifest(manifestPath, signaturePath, signingKeyPath)
    finally:
        signingKeyPath.unlink(missing_ok=True)
    build["custom_ota"] = {
        "manifest": customBuild.fileMetadata(manifestPath),
        "signature": customBuild.fileMetadata(signaturePath),
    }
    buildManifestPath.write_text(json.dumps(build, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    writeArtifacts(
        args.build_dir.resolve(),
        args.build_manifest.resolve(),
        args.base_url,
        os.environ.get("HDS_CUSTOM_OTA_SIGNING_KEY_PEM", ""),
    )


if __name__ == "__main__":
    main()
