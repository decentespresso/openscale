#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


OUTPUT = Path(".pio.nosync") / "generated" / "include" / "ota_public_key.h"
KEY_FILES = tuple(
    Path("keys") / "ota" / f"hds_ota_manifest_public_key_{index}.pem"
    for index in range(1, 4)
)


def openssl_path():
    executable = shutil.which(os.environ.get("OPENSSL", "openssl"))
    if executable is None:
        raise SystemExit("OpenSSL is required; set OPENSSL to its executable path")
    return executable


def canonical_public_key_der(executable, path):
    result = subprocess.run(
        [
            executable,
            "pkey",
            "-pubin",
            "-in",
            str(path),
            "-pubout",
            "-outform",
            "DER",
        ],
        capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(f"invalid OTA public key file: {path}")
    return result.stdout


def public_key_pems():
    key_files = []
    for path in KEY_FILES:
        if not path.is_file():
            raise SystemExit(f"missing OTA public key file: {path}")
        try:
            pem = path.read_text(encoding="utf-8").strip()
        except UnicodeError:
            raise SystemExit(f"invalid OTA public key file: {path}")
        key_files.append((path, pem))

    executable = openssl_path()
    fingerprints = [
        hashlib.sha256(canonical_public_key_der(executable, path)).digest()
        for path, _ in key_files
    ]
    if len(set(fingerprints)) != len(fingerprints):
        raise SystemExit("OTA public keys must be distinct")
    return [pem for _, pem in key_files]


def main():
    defines = "".join(
        f"#define HDS_OTA_MANIFEST_PUBLIC_KEY_{index}_PEM "
        f"{json.dumps(pem + chr(10))}\n"
        for index, pem in enumerate(public_key_pems(), 1)
    )
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("#pragma once\n" + defines, encoding="utf-8")
    sys.stdout.write(f"wrote {OUTPUT}\n")


if __name__ == "__main__":
    main()
