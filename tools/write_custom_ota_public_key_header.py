#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


OUTPUT = Path(".pio.nosync") / "generated" / "include" / "custom_ota_public_key.h"
KEY_FILES = tuple(
    Path("keys") / "ota" / f"hds_custom_ota_manifest_public_key_{index}.pem"
    for index in range(1, 3)
)


def opensslPath():
    executable = shutil.which(os.environ.get("OPENSSL", "openssl"))
    if executable:
        return executable
    git = shutil.which("git")
    if git:
        gitRoot = Path(git).resolve().parent.parent
        for relative in ("usr/bin/openssl.exe", "mingw64/bin/openssl.exe", "mingw32/bin/openssl.exe"):
            candidate = gitRoot / relative
            if candidate.is_file():
                return str(candidate)
    raise SystemExit("OpenSSL is required; set OPENSSL to its executable path")


def publicKey(publicPath):
    if not publicPath.is_file():
        raise SystemExit(f"missing custom OTA public key file: {publicPath}")
    result = subprocess.run(
        [opensslPath(), "pkey", "-pubin", "-in", str(publicPath), "-pubout", "-outform", "DER"],
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        raise SystemExit(f"invalid custom OTA public key file: {publicPath}")
    return publicPath.read_text(encoding="utf-8").strip() + "\n", hashlib.sha256(result.stdout).digest()


def publicKeys():
    paths = tuple(
        Path(os.environ.get(f"HDS_CUSTOM_OTA_PUBLIC_KEY_{index}_FILE", default))
        for index, default in enumerate(KEY_FILES, 1)
    )
    keys = tuple(publicKey(path) for path in paths)
    if len({fingerprint for _, fingerprint in keys}) != len(keys):
        raise SystemExit("custom OTA public keys must be distinct")
    return tuple(pem for pem, _ in keys)


def main():
    keys = publicKeys()
    combinationHash = os.environ.get("HDS_CUSTOM_BUILD_COMBINATION_HASH", "")
    if combinationHash and (len(combinationHash) != 64 or any(
        character not in "0123456789abcdef" for character in combinationHash
    )):
        raise SystemExit("invalid HDS_CUSTOM_BUILD_COMBINATION_HASH")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    defines = "".join(
        f"#define HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_{index}_PEM {json.dumps(public)}\n"
        for index, public in enumerate(keys, 1)
    )
    OUTPUT.write_text(
        "#pragma once\n" + defines +
        "#define HDS_CUSTOM_BUILD_COMBINATION_HASH " + json.dumps(combinationHash) + "\n",
        encoding="utf-8",
    )


try:
    Import("env")
except NameError:
    if __name__ == "__main__":
        main()
else:
    main()
