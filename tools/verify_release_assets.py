import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


def requireEqual(actual, expected, description):
    if actual != expected:
        raise ValueError(description)


def verifyAssets(downloadDir, expectedDir, buildDir, keyDir, tag):
    manifestPath = downloadDir / "manifest.json"
    signaturePath = downloadDir / "manifest.sig"
    verified = any(
        subprocess.run(
            ["openssl", "dgst", "-sha256", "-verify", str(keyDir / f"hds_ota_manifest_public_key_{index}.pem"),
             "-signature", str(signaturePath), str(manifestPath)],
            capture_output=True,
        ).returncode == 0
        for index in range(1, 4)
    )
    if not verified:
        raise ValueError("Downloaded manifest signature is invalid")
    for name in ("manifest.json", "manifest.sig", "dependencies.txt"):
        requireEqual((downloadDir / name).read_bytes(), (expectedDir / name).read_bytes(),
                     f"Downloaded {name} differs from prepared release")
    manifest = json.loads(manifestPath.read_bytes())
    requireEqual(manifest["version"], tag.removeprefix("v"), "Downloaded manifest has wrong version")
    for asset in ("firmware", "littlefs"):
        name = f"{asset}.bin"
        data = (downloadDir / name).read_bytes()
        requireEqual(len(data), manifest[asset]["size"], f"Downloaded {name} has wrong size")
        requireEqual(hashlib.sha256(data).hexdigest(), manifest[asset]["sha256"],
                     f"Downloaded {name} has wrong SHA-256")
        requireEqual(data, (buildDir / name).read_bytes(), f"Downloaded {name} differs from build")
    with zipfile.ZipFile(downloadDir / f"HDS_FW_{tag}.zip") as archive:
        names = ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin")
        requireEqual(sorted(archive.namelist()), sorted(names), "Downloaded ZIP has unexpected entries")
        for name in names:
            entry = archive.getinfo(name)
            expected = (buildDir / name).read_bytes()
            requireEqual(entry.file_size, len(expected), f"ZIP {name} has wrong size")
            requireEqual(archive.read(entry), expected, f"ZIP {name} differs from build")


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
