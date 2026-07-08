#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path


OUTPUT = Path(".pio.nosync") / "generated" / "include" / "ota_public_key.h"


def public_key_pem():
    key_file = os.environ.get("HDS_OTA_MANIFEST_PUBLIC_KEY_FILE")
    if key_file:
        return Path(key_file).read_text(encoding="utf-8").strip()
    return os.environ.get("HDS_OTA_MANIFEST_PUBLIC_KEY_PEM", "").strip()


def main():
    pem = public_key_pem()
    if not pem:
        raise SystemExit("missing HDS_OTA_MANIFEST_PUBLIC_KEY_PEM")
    public_key_labels = ("PUBLIC KEY", "RSA PUBLIC KEY", "EC PUBLIC KEY")
    has_label = any(
        f"-----BEGIN {label}-----" in pem and f"-----END {label}-----" in pem
        for label in public_key_labels
    )
    if not has_label:
        raise SystemExit("HDS_OTA_MANIFEST_PUBLIC_KEY_PEM must be a PEM public key")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        "#pragma once\n"
        f"#define HDS_OTA_MANIFEST_PUBLIC_KEY_PEM {json.dumps(pem + chr(10))}\n",
        encoding="utf-8",
    )
    sys.stdout.write(f"wrote {OUTPUT}\n")


if __name__ == "__main__":
    main()
