import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request

import build_custom_firmware as customRunner
import configure_custom_build as customBuild


HASH_PATTERN = re.compile(r"^[0-9a-f]{64}$")
MAX_ENTRY_BYTES = 5 * 1024 * 1024


def loadEntry(entryDir):
    combinationHash = entryDir.name
    if not HASH_PATTERN.fullmatch(combinationHash):
        raise ValueError("cache entry directory must be a combination hash")
    try:
        manifest = json.loads((entryDir / "build-manifest.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("cache entry has no valid build manifest") from error
    identity = manifest.get("combination_input")
    if customBuild.combinationHash(identity) != combinationHash:
        raise ValueError("cache entry identity does not match its directory")
    if not customRunner.completeCacheEntry(entryDir, combinationHash, identity):
        raise ValueError("cache entry is incomplete")
    if sum((entryDir / name).stat().st_size for name in customRunner.BUILD_FILES) > MAX_ENTRY_BYTES:
        raise ValueError("cache entry exceeds 5 MiB")
    return combinationHash


def publishFile(baseUrl, token, combinationHash, path):
    payload = path.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    request = urllib.request.Request(
        f"{baseUrl}/internal/v1/{combinationHash}/{path.name}",
        data=payload,
        method="PUT",
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json" if path.suffix == ".json" else "application/octet-stream",
            "User-Agent": "OpenScale-Custom-Build/1.0",
            "X-OpenScale-SHA256": digest,
        },
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        if response.status != 201:
            raise ValueError(f"upload failed for {path.name}: HTTP {response.status}")


def publishEntry(entryDir, baseUrl, token):
    parsed = urllib.parse.urlsplit(baseUrl)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError("publish URL must be HTTPS without credentials")
    if parsed.query or parsed.fragment:
        raise ValueError("publish URL cannot contain a query or fragment")
    if len(token) < 32:
        raise ValueError("upload token must contain at least 32 characters")
    combinationHash = loadEntry(entryDir)
    orderedFiles = [name for name in customRunner.BUILD_FILES if name != "build-manifest.json"]
    orderedFiles.append("build-manifest.json")
    for name in orderedFiles:
        publishFile(baseUrl.rstrip("/"), token, combinationHash, entryDir / name)
    return combinationHash


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--entry", type=Path, required=True)
    parser.add_argument("--base-url", required=True)
    args = parser.parse_args()
    try:
        combinationHash = publishEntry(
            args.entry.resolve(), args.base_url, os.environ.get("CUSTOM_BUILD_UPLOAD_TOKEN", "")
        )
    except (ValueError, urllib.error.HTTPError, urllib.error.URLError) as error:
        parser.exit(1, f"custom build publication failed: {error}\n")
    print(json.dumps({"combination_hash": combinationHash}, sort_keys=True))


if __name__ == "__main__":
    main()
