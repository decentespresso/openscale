#!/usr/bin/env python3
import json
import sys
from pathlib import Path


OUTPUT = Path(".pio.nosync") / "generated" / "include" / "ota_public_key.h"
KEY_FILES = tuple(
    Path("keys") / "ota" / f"hds_ota_manifest_public_key_{index}.pem"
    for index in range(1, 4)
)


def public_key_pems():
    pems = []
    for path in KEY_FILES:
        if not path.is_file():
            raise SystemExit(f"missing OTA public key file: {path}")
        pem = path.read_text(encoding="utf-8").strip()
        public_key_labels = ("PUBLIC KEY", "RSA PUBLIC KEY", "EC PUBLIC KEY")
        if not any(
            f"-----BEGIN {label}-----" in pem and f"-----END {label}-----" in pem
            for label in public_key_labels
        ):
            raise SystemExit(f"invalid OTA public key file: {path}")
        pems.append(pem)
    if len(set(pems)) != len(pems):
        raise SystemExit("OTA public keys must be distinct")
    return pems


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
