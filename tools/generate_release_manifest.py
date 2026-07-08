#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path


DEFAULT_REPOSITORY = "decentespresso/openscale"
DEFAULT_MODEL = "hds"
DEFAULT_MIN_FROM = "3.0.0"
DEFAULT_CHIP = "esp32s3"
DEFAULT_ENVIRONMENT = "esp32s3"
DEFAULT_FLASH_SIZE = 8388608
DEFAULT_PARTITION_SCHEMA = "esp32s3-default-8mb-ota-spiffs-1536k"
DEFAULT_APP_PARTITION_MIN_SIZE = 3342336
DEFAULT_FS_PARTITION_LABEL = "spiffs"
DEFAULT_FS_PARTITION_SIZE = 1572864
DEFAULT_FS_SCHEMA = 1
DEFAULT_CATALOG_MIN_VERSION = "v3.1.13"
STABLE_VERSION_RE = re.compile(r"^v?([0-9]+)\.([0-9]+)\.([0-9]+)$")


def clean_version(tag):
    match = STABLE_VERSION_RE.fullmatch(tag.strip())
    if not match:
        raise ValueError(f"version must be stable major.minor.patch: {tag}")
    return ".".join(match.groups())


def version_key(version):
    return tuple(int(part) for part in clean_version(version).split("."))


def maybe_version_key(version):
    try:
        return version_key(version)
    except (TypeError, ValueError):
        return None


def detect_active_board(config_path):
    text = config_path.read_text(encoding="utf-8")
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        match = re.match(r"#define\s+(V\d[\w_]*)\b", stripped)
        if match:
            return match.group(1)
    return None


def detect_pcb_version(config_path):
    text = config_path.read_text(encoding="utf-8")
    active_board = detect_active_board(config_path)
    if active_board:
        start = text.find(f"#ifdef {active_board}")
        if start >= 0:
            next_board = text.find("\n#ifdef V", start + len(active_board))
            block = text[start:] if next_board < 0 else text[start:next_board]
            match = re.search(r'#define\s+PCB_VER\s+\(char\*\)"([^"]+)"', block)
            if match:
                return match.group(1)
    match = re.search(r'#define\s+PCB_VER\s+\(char\*\)"([^"]+)"', text)
    return match.group(1) if match else None


def github_release_download_url(repository, tag, asset_name):
    return f"https://github.com/{repository}/releases/download/{tag}/{asset_name}"


def github_release_notes_url(repository, tag):
    return f"https://github.com/{repository}/releases/tag/{tag}"


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def asset_manifest(build_dir, repository, tag, asset_name):
    path = build_dir / asset_name
    if not path.is_file():
        raise FileNotFoundError(path)
    return {
        "url": github_release_download_url(repository, tag, asset_name),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def build_manifest(
    build_dir,
    tag,
    repository,
    model,
    min_from,
    pcb=None,
    chip=DEFAULT_CHIP,
    environment=DEFAULT_ENVIRONMENT,
    flash_size=DEFAULT_FLASH_SIZE,
    partition_schema=DEFAULT_PARTITION_SCHEMA,
    app_partition_min_size=DEFAULT_APP_PARTITION_MIN_SIZE,
    fs_partition_label=DEFAULT_FS_PARTITION_LABEL,
    fs_partition_size=DEFAULT_FS_PARTITION_SIZE,
    fs_schema=DEFAULT_FS_SCHEMA,
    release_notes_url=None,
):
    build_dir = Path(build_dir)
    version = clean_version(tag)
    min_from_version = clean_version(min_from) if min_from else ""
    manifest = {
        "model": model,
        "version": version,
        "min_from": min_from_version,
        "release_notes_url": release_notes_url or github_release_notes_url(repository, tag),
        "chip": chip,
        "environment": environment,
        "flash_size": int(flash_size),
        "partition_schema": partition_schema,
        "app_partition_min_size": int(app_partition_min_size),
        "fs_partition_label": fs_partition_label,
        "fs_partition_size": int(fs_partition_size),
        "fs_schema": int(fs_schema),
        "firmware": asset_manifest(build_dir, repository, tag, "firmware.bin"),
    }
    if pcb:
        manifest["pcb"] = pcb
    littlefs_path = build_dir / "littlefs.bin"
    if not littlefs_path.is_file():
        raise FileNotFoundError(littlefs_path)
    littlefs = asset_manifest(build_dir, repository, tag, "littlefs.bin")
    littlefs["required"] = True
    manifest["littlefs"] = littlefs
    return manifest


def write_manifest(manifest, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def release_entries_from_manifest(manifest):
    releases = manifest.get("releases")
    if isinstance(releases, list):
        return releases
    return [manifest]


def release_key(manifest):
    return (
        manifest.get("model"),
        manifest.get("pcb"),
        manifest.get("version"),
        manifest.get("chip"),
        manifest.get("environment"),
    )


def build_catalog_manifest(latest_manifest, previous_manifests, min_version=None):
    catalog = dict(latest_manifest)
    releases = []
    seen = set()
    min_key = version_key(min_version) if min_version else None
    for manifest in [latest_manifest, *previous_manifests]:
        for release in release_entries_from_manifest(manifest):
            if not isinstance(release, dict):
                continue
            release_version_key = maybe_version_key(release.get("version"))
            if release_version_key is None:
                continue
            if min_key and release_version_key < min_key:
                continue
            key = release_key(release)
            if key in seen:
                continue
            releases.append(release)
            seen.add(key)
    releases.sort(key=lambda release: version_key(release["version"]), reverse=True)
    catalog["releases"] = releases
    return catalog


def sign_manifest(manifest_path, signature_path, signing_key_path):
    subprocess.run(
        [
            "openssl",
            "dgst",
            "-sha256",
            "-sign",
            str(signing_key_path),
            "-out",
            str(signature_path),
            str(manifest_path),
        ],
        check=True,
    )


def sign_manifest_from_environment(manifest_path, signature_path):
    signing_key_file = os.environ.get("HDS_OTA_SIGNING_KEY_FILE")
    signing_key_pem = os.environ.get("HDS_OTA_SIGNING_KEY_PEM")
    if signing_key_file:
        sign_manifest(manifest_path, signature_path, Path(signing_key_file))
        return True
    if signing_key_pem:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as handle:
            handle.write(signing_key_pem)
            temp_key_path = Path(handle.name)
        try:
            sign_manifest(manifest_path, signature_path, temp_key_path)
        finally:
            temp_key_path.unlink(missing_ok=True)
        return True
    return False


def parser():
    default_build_dir = Path(".pio.nosync") / "build" / "esp32s3"
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=default_build_dir)
    parser.add_argument("--output-dir", type=Path, default=Path("release-files"))
    parser.add_argument("--tag", default=os.environ.get("HDS_RELEASE_TAG") or os.environ.get("GITHUB_REF_NAME"))
    parser.add_argument("--repository", default=os.environ.get("HDS_RELEASE_REPOSITORY") or os.environ.get("GITHUB_REPOSITORY") or DEFAULT_REPOSITORY)
    parser.add_argument("--model", default=os.environ.get("HDS_RELEASE_MODEL", DEFAULT_MODEL))
    parser.add_argument("--min-from", default=os.environ.get("HDS_RELEASE_MIN_FROM", DEFAULT_MIN_FROM))
    parser.add_argument("--release-notes-url", default=os.environ.get("HDS_RELEASE_NOTES_URL"))
    parser.add_argument("--config", type=Path, default=Path("include") / "config.h")
    parser.add_argument("--pcb", default=os.environ.get("HDS_RELEASE_PCB"))
    parser.add_argument("--chip", default=os.environ.get("HDS_RELEASE_CHIP", DEFAULT_CHIP))
    parser.add_argument("--environment", default=os.environ.get("HDS_RELEASE_ENVIRONMENT", DEFAULT_ENVIRONMENT))
    parser.add_argument("--flash-size", type=int, default=int(os.environ.get("HDS_RELEASE_FLASH_SIZE", DEFAULT_FLASH_SIZE)))
    parser.add_argument("--partition-schema", default=os.environ.get("HDS_RELEASE_PARTITION_SCHEMA", DEFAULT_PARTITION_SCHEMA))
    parser.add_argument("--app-partition-min-size", type=int, default=int(os.environ.get("HDS_RELEASE_APP_PARTITION_MIN_SIZE", DEFAULT_APP_PARTITION_MIN_SIZE)))
    parser.add_argument("--fs-partition-label", default=os.environ.get("HDS_RELEASE_FS_PARTITION_LABEL", DEFAULT_FS_PARTITION_LABEL))
    parser.add_argument("--fs-partition-size", type=int, default=int(os.environ.get("HDS_RELEASE_FS_PARTITION_SIZE", DEFAULT_FS_PARTITION_SIZE)))
    parser.add_argument("--fs-schema", type=int, default=int(os.environ.get("HDS_RELEASE_FS_SCHEMA", DEFAULT_FS_SCHEMA)))
    parser.add_argument("--catalog", action="store_true")
    parser.add_argument("--previous-manifest", type=Path, action="append", default=[])
    parser.add_argument("--catalog-min-version", default=os.environ.get("HDS_RELEASE_CATALOG_MIN_VERSION", DEFAULT_CATALOG_MIN_VERSION))
    return parser


def main():
    args = parser().parse_args()
    if not args.tag:
        raise SystemExit("missing --tag or HDS_RELEASE_TAG")
    manifest = build_manifest(
        build_dir=args.build_dir,
        tag=args.tag,
        repository=args.repository,
        model=args.model,
        min_from=args.min_from,
        pcb=args.pcb if args.pcb is not None else detect_pcb_version(args.config),
        chip=args.chip,
        environment=args.environment,
        flash_size=args.flash_size,
        partition_schema=args.partition_schema,
        app_partition_min_size=args.app_partition_min_size,
        fs_partition_label=args.fs_partition_label,
        fs_partition_size=args.fs_partition_size,
        fs_schema=args.fs_schema,
        release_notes_url=args.release_notes_url,
    )
    if args.catalog:
        previous_manifests = [
            json.loads(path.read_text(encoding="utf-8"))
            for path in args.previous_manifest
        ]
        manifest = build_catalog_manifest(
            manifest,
            previous_manifests,
            min_version=args.catalog_min_version,
        )
    manifest_path = args.output_dir / "manifest.json"
    write_manifest(manifest, manifest_path)
    signature_path = args.output_dir / "manifest.sig"
    signed = sign_manifest_from_environment(manifest_path, signature_path)
    print(f"wrote {manifest_path}")
    if signed:
        print(f"wrote {signature_path}")


if __name__ == "__main__":
    main()
