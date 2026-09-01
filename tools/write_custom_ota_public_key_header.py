#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


OUTPUT = Path(".pio.nosync") / "generated" / "include" / "custom_ota_public_key.h"


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


def publicKey(signingKey):
    if not signingKey:
        return ""
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as handle:
        handle.write(signingKey)
        privatePath = Path(handle.name)
    try:
        result = subprocess.run(
            [opensslPath(), "pkey", "-in", str(privatePath), "-pubout"],
            check=False,
            capture_output=True,
            text=True,
        )
    finally:
        privatePath.unlink(missing_ok=True)
    if result.returncode != 0 or "BEGIN PUBLIC KEY" not in result.stdout:
        raise SystemExit("invalid HDS_CUSTOM_OTA_SIGNING_KEY_PEM")
    return result.stdout.strip() + "\n"


def main():
    public = publicKey(os.environ.get("HDS_CUSTOM_OTA_SIGNING_KEY_PEM", ""))
    combinationHash = os.environ.get("HDS_CUSTOM_BUILD_COMBINATION_HASH", "")
    if combinationHash and (len(combinationHash) != 64 or any(
        character not in "0123456789abcdef" for character in combinationHash
    )):
        raise SystemExit("invalid HDS_CUSTOM_BUILD_COMBINATION_HASH")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        "#pragma once\n"
        "#define HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_PEM " + json.dumps(public) + "\n"
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
