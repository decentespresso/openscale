import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from generate_release_manifest import MAX_MANIFEST_BYTES, build_catalog_manifest


def requireEqual(actual, expected, description):
    if actual != expected:
        raise ValueError(description)


def verifySignature(manifestPath, signaturePath, keyDir):
    verified = any(
        subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", str(keyDir / f"hds_ota_manifest_public_key_{index}.pem"),
             "-signature", str(signaturePath), str(manifestPath)],
            capture_output=True,
        ).returncode == 0
        for index in range(1, 4)
    )
    if not verified:
        raise ValueError(f"Downloaded signature is invalid: {manifestPath.name}")


def signedJson(path, signaturePath, keyDir):
    if not 0 < path.stat().st_size <= MAX_MANIFEST_BYTES:
        raise ValueError("Signed JSON has invalid size")
    verifySignature(path, signaturePath, keyDir)
    value = json.loads(path.read_bytes())
    if not isinstance(value, dict):
        raise ValueError("Signed JSON must be an object")
    return value


def fileRecord(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Missing regular asset: {path.name}")
    with path.open("rb") as handle:
        digest = hashlib.file_digest(handle, "sha256").hexdigest()
    return {"size": path.stat().st_size, "sha256": digest}


def verifyCatalog(manifest, previous):
    current = {key: value for key, value in manifest.items() if key != "releases"}
    expected = build_catalog_manifest(current, [previous] if previous else [], "v3.1.13")
    requireEqual(manifest.get("releases"), expected["releases"], "Signed catalog history changed")


def verifyAssets(downloadDir, expectedDir, buildDir, keyDir, tag, previous=None):
    manifest = signedJson(downloadDir / "manifest.json", downloadDir / "manifest.sig", keyDir)
    if expectedDir is not None:
        for name in ("manifest.json", "manifest.sig", "dependencies.txt"):
            requireEqual((downloadDir / name).read_bytes(), (expectedDir / name).read_bytes(),
                         f"Downloaded {name} differs from prepared release")
    requireEqual(manifest["version"], tag.removeprefix("v"), "Downloaded manifest has wrong version")
    if previous is not None:
        verifyCatalog(manifest, previous)
    if manifest["littlefs"].get("required") is not True:
        raise ValueError("LittleFS must remain required")
    if (downloadDir / "dependencies.txt").stat().st_size == 0:
        raise ValueError("Dependency inventory is empty")
    for asset in ("firmware", "littlefs"):
        name = f"{asset}.bin"
        data = (downloadDir / name).read_bytes()
        requireEqual(len(data), manifest[asset]["size"], f"Downloaded {name} has wrong size")
        requireEqual(hashlib.sha256(data).hexdigest(), manifest[asset]["sha256"],
                     f"Downloaded {name} has wrong SHA-256")
        if asset == "firmware" and b"FW: " + manifest["version"].encode("ascii") + b"\0" not in data:
            raise ValueError("Firmware does not contain the exact release version")
        if buildDir is not None:
            requireEqual(data, (buildDir / name).read_bytes(), f"Downloaded {name} differs from build")
    with zipfile.ZipFile(downloadDir / f"HDS_FW_{tag}.zip") as archive:
        names = ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin")
        requireEqual(sorted(archive.namelist()), sorted(names), "Downloaded ZIP has unexpected entries")
        for name in names:
            entry = archive.getinfo(name)
            if not 0 < entry.file_size <= 8388608:
                raise ValueError(f"ZIP {name} has invalid size")
            data = archive.read(entry)
            if name in ("firmware.bin", "littlefs.bin"):
                requireEqual(data, (downloadDir / name).read_bytes(), f"ZIP {name} differs from OTA asset")
            if buildDir is not None:
                expected = (buildDir / name).read_bytes()
                requireEqual(entry.file_size, len(expected), f"ZIP {name} has wrong size")
                requireEqual(data, expected, f"ZIP {name} differs from build")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", required=True)
    parser.add_argument("--download-dir", type=Path, required=True)
    parser.add_argument("--expected-dir", type=Path, default=Path("release-files"))
    parser.add_argument("--build-dir", type=Path, default=Path(".pio.nosync/build/esp32s3"))
    parser.add_argument("--key-dir", type=Path, default=Path("keys/ota"))
    args = parser.parse_args()
    verifyAssets(args.download_dir, args.expected_dir, args.build_dir, args.key_dir, args.tag)
    print("Downloaded release assets verified")


if __name__ == "__main__":
    main()
