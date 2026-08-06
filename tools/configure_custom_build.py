import argparse
import hashlib
import json
import os
import re
import shutil
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


FEATURE_IDS = (
    "wifi",
    "mdns",
    "webserver",
    "websocket",
    "littlefs",
    "elegant_ota",
    "pull_ota",
    "grinder",
)
FEATURE_DEPENDENCIES = {
    "wifi": (),
    "mdns": ("wifi",),
    "webserver": ("wifi",),
    "websocket": ("wifi", "webserver"),
    "littlefs": (),
    "elegant_ota": ("wifi", "webserver"),
    "pull_ota": ("wifi",),
    "grinder": ("wifi", "mdns"),
}
FEATURE_MACROS = {
    "wifi": "HDS_FEATURE_WIFI",
    "mdns": "HDS_FEATURE_MDNS",
    "webserver": "HDS_FEATURE_WEBSERVER",
    "websocket": "HDS_FEATURE_WEBSOCKET",
    "littlefs": "HDS_FEATURE_LITTLEFS",
    "elegant_ota": "HDS_FEATURE_ELEGANT_OTA",
    "pull_ota": "HDS_FEATURE_PULL_OTA",
    "grinder": "HDS_FEATURE_GRINDER",
}
SUPPORTED_FIRMWARE_REFS = ("main",)
IDENTIFIER = re.compile(r"^[a-z][a-z0-9-]*$")
SEMVER = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
PLUGIN_KEYS = {
    "schema",
    "id",
    "name",
    "version",
    "firmware_refs",
    "requires",
    "conflicts",
    "assets",
    "budget",
}
SELECTION_KEYS = {"firmware_ref", "features", "plugins"}
BUDGET_KEYS = {"firmware_flash_bytes", "static_ram_bytes", "littlefs_bytes"}
ASSET_KEYS = {"source", "target"}
ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


class ConfigurationError(ValueError):
    pass


def strict_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ConfigurationError(f"duplicate JSON key: {key}")
        value[key] = item
    return value


@dataclass(frozen=True)
class Plugin:
    plugin_id: str
    name: str
    version: str
    firmware_refs: tuple
    requires: tuple
    conflicts: tuple
    assets: tuple
    budget: dict
    directory: Path


@dataclass(frozen=True)
class ResolvedBuild:
    firmware_ref: str
    features: tuple
    plugins: tuple


def load_object(path):
    try:
        value = json.loads(
            Path(path).read_text(encoding="utf-8"), object_pairs_hook=strict_object
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigurationError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ConfigurationError(f"JSON root must be an object: {path}")
    return value


def require_exact_keys(value, expected, label):
    keys = set(value)
    missing = sorted(expected - keys)
    unknown = sorted(keys - expected)
    if missing or unknown:
        raise ConfigurationError(f"{label} keys missing={missing} unknown={unknown}")


def require_string_list(value, label):
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ConfigurationError(f"{label} must be a string list")
    if len(value) != len(set(value)):
        raise ConfigurationError(f"{label} contains duplicates")
    return tuple(value)


def safe_relative_path(value, label):
    if not isinstance(value, str) or not value or "\\" in value:
        raise ConfigurationError(f"{label} must be a safe relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise ConfigurationError(f"{label} must be a safe relative path")
    return path


def load_plugin(path):
    value = load_object(path)
    require_exact_keys(value, PLUGIN_KEYS, str(path))
    if value["schema"] != 1:
        raise ConfigurationError(f"unsupported plugin schema: {path}")
    plugin_id = value["id"]
    if not isinstance(plugin_id, str) or not IDENTIFIER.fullmatch(plugin_id):
        raise ConfigurationError(f"unsafe plugin id: {plugin_id}")
    if Path(path).parent.name != plugin_id:
        raise ConfigurationError(f"plugin directory must match id: {plugin_id}")
    if not isinstance(value["name"], str) or not value["name"].strip():
        raise ConfigurationError(f"plugin name is required: {plugin_id}")
    if not isinstance(value["version"], str) or not SEMVER.fullmatch(value["version"]):
        raise ConfigurationError(f"plugin version must be SemVer: {plugin_id}")
    firmware_refs = require_string_list(value["firmware_refs"], f"{plugin_id}.firmware_refs")
    if not firmware_refs or any(ref not in SUPPORTED_FIRMWARE_REFS for ref in firmware_refs):
        raise ConfigurationError(f"unsupported firmware ref in {plugin_id}")
    requires = require_string_list(value["requires"], f"{plugin_id}.requires")
    unknown_features = sorted(set(requires) - set(FEATURE_IDS))
    if unknown_features:
        raise ConfigurationError(f"unknown feature dependencies for {plugin_id}: {unknown_features}")
    conflicts = require_string_list(value["conflicts"], f"{plugin_id}.conflicts")
    if not isinstance(value["assets"], list):
        raise ConfigurationError(f"{plugin_id}.assets must be a list")
    assets = []
    directory = Path(path).parent
    for index, asset in enumerate(value["assets"]):
        if not isinstance(asset, dict):
            raise ConfigurationError(f"{plugin_id}.assets[{index}] must be an object")
        require_exact_keys(asset, ASSET_KEYS, f"{plugin_id}.assets[{index}]")
        source = safe_relative_path(asset["source"], f"{plugin_id}.assets[{index}].source")
        target = safe_relative_path(asset["target"], f"{plugin_id}.assets[{index}].target")
        source_path = directory.joinpath(*source.parts)
        if not source_path.is_file() or directory.resolve() not in source_path.resolve().parents:
            raise ConfigurationError(f"missing or unsafe plugin asset: {source_path}")
        assets.append((source, target))
    budget = value["budget"]
    if not isinstance(budget, dict):
        raise ConfigurationError(f"{plugin_id}.budget must be an object")
    require_exact_keys(budget, BUDGET_KEYS, f"{plugin_id}.budget")
    if any(type(budget[key]) is not int or budget[key] < 0 for key in BUDGET_KEYS):
        raise ConfigurationError(f"{plugin_id}.budget values must be non-negative integers")
    return Plugin(
        plugin_id,
        value["name"].strip(),
        value["version"],
        firmware_refs,
        requires,
        conflicts,
        tuple(assets),
        dict(budget),
        directory,
    )


def load_catalog(project_root):
    plugins_root = Path(project_root) / "plugins"
    plugins = {}
    targets = {}
    for path in sorted(plugins_root.glob("*/plugin.json")):
        plugin = load_plugin(path)
        if plugin.plugin_id in plugins:
            raise ConfigurationError(f"duplicate plugin id: {plugin.plugin_id}")
        plugins[plugin.plugin_id] = plugin
        for _, target in plugin.assets:
            target_text = target.as_posix()
            if target_text in targets:
                raise ConfigurationError(
                    f"asset target collision: {target_text} ({targets[target_text]}, {plugin.plugin_id})"
                )
            targets[target_text] = plugin.plugin_id
    known_conflicts = set(plugins) | set(FEATURE_IDS)
    for plugin in plugins.values():
        unknown = sorted(set(plugin.conflicts) - known_conflicts)
        if unknown:
            raise ConfigurationError(f"unknown plugin conflicts for {plugin.plugin_id}: {unknown}")
        asset_bytes = sum(
            plugin.directory.joinpath(*source.parts).stat().st_size
            for source, _ in plugin.assets
        )
        if asset_bytes > plugin.budget["littlefs_bytes"]:
            raise ConfigurationError(
                f"{plugin.plugin_id} assets use {asset_bytes} bytes, above its LittleFS budget"
            )
    return plugins


def load_selection(path, plugins):
    value = load_object(path)
    require_exact_keys(value, SELECTION_KEYS, str(path))
    firmware_ref = value["firmware_ref"]
    if firmware_ref not in SUPPORTED_FIRMWARE_REFS:
        raise ConfigurationError(f"unsupported firmware_ref: {firmware_ref}")
    features = require_string_list(value["features"], "features")
    plugin_ids = require_string_list(value["plugins"], "plugins")
    unknown_features = sorted(set(features) - set(FEATURE_IDS))
    unknown_plugins = sorted(set(plugin_ids) - set(plugins))
    if unknown_features:
        raise ConfigurationError(f"unknown features: {unknown_features}")
    if unknown_plugins:
        raise ConfigurationError(f"unknown plugins: {unknown_plugins}")
    return firmware_ref, features, plugin_ids


def validate_resolved(features, plugin_ids, plugins):
    feature_set = set(features)
    for feature in features:
        missing = sorted(set(FEATURE_DEPENDENCIES[feature]) - feature_set)
        if missing:
            raise ConfigurationError(f"{feature} missing dependencies: {missing}")
    for plugin_id in plugin_ids:
        plugin = plugins[plugin_id]
        missing = sorted(set(plugin.requires) - feature_set)
        if missing:
            raise ConfigurationError(f"{plugin_id} missing dependencies: {missing}")
        conflicts = sorted(set(plugin.conflicts) & (set(plugin_ids) | feature_set))
        if conflicts:
            raise ConfigurationError(f"{plugin_id} conflicts with: {conflicts}")


def resolve_selection(path, project_root):
    plugins = load_catalog(project_root)
    firmware_ref, requested_features, plugin_ids = load_selection(path, plugins)
    resolved = set(requested_features)
    if "wifi" in requested_features:
        resolved.add("webserver")
    for plugin_id in plugin_ids:
        plugin = plugins[plugin_id]
        if firmware_ref not in plugin.firmware_refs:
            raise ConfigurationError(f"{plugin_id} does not support {firmware_ref}")
        resolved.update(plugin.requires)
    changed = True
    while changed:
        changed = False
        for feature in tuple(resolved):
            before = len(resolved)
            resolved.update(FEATURE_DEPENDENCIES[feature])
            changed = changed or len(resolved) != before
    ordered_features = tuple(feature for feature in FEATURE_IDS if feature in resolved)
    ordered_plugins = tuple(sorted(plugin_ids))
    validate_resolved(ordered_features, ordered_plugins, plugins)
    return ResolvedBuild(firmware_ref, ordered_features, ordered_plugins), plugins


def generated_header(resolved):
    enabled = set(resolved.features)
    lines = ["#ifndef GENERATED_FEATURES_H", "#define GENERATED_FEATURES_H", ""]
    for feature in FEATURE_IDS:
        lines.append(f"#define {FEATURE_MACROS[feature]} {1 if feature in enabled else 0}")
    lines.extend(
        [
            f"#define HDS_ENABLE_GRINDER {1 if 'grinder' in enabled else 0}",
            f'#define HDS_CUSTOM_FIRMWARE_REF "{resolved.firmware_ref}"',
            "",
            "#endif",
            "",
        ]
    )
    return "\n".join(lines)


def copy_assets(project_root, output_root, resolved, plugins):
    data_dir = output_root / "data"
    targets = {}
    if "littlefs" not in resolved.features:
        data_dir.mkdir(parents=True, exist_ok=True)
        return data_dir
    base_dir = project_root / "web_apps"
    for source in sorted(path for path in base_dir.rglob("*") if path.is_file()):
        if source.suffix == ".gz":
            continue
        target = source.relative_to(base_dir).as_posix()
        targets[target] = source
    for plugin_id in resolved.plugins:
        plugin = plugins[plugin_id]
        for source, target in plugin.assets:
            target_text = target.as_posix()
            if target_text in targets:
                raise ConfigurationError(f"asset target collision: {target_text}")
            targets[target_text] = plugin.directory.joinpath(*source.parts)
    for target, source in sorted(targets.items()):
        destination = data_dir.joinpath(*PurePosixPath(target).parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    return data_dir


def normalized_selection(resolved, plugins):
    return {
        "firmware_ref": resolved.firmware_ref,
        "features": list(resolved.features),
        "plugins": [
            {"id": plugin_id, "version": plugins[plugin_id].version}
            for plugin_id in resolved.plugins
        ],
    }


def prepare_build(config_path, project_root, output_root):
    project_root = Path(project_root).resolve()
    output_root = Path(output_root).resolve()
    if output_root == project_root or output_root in project_root.parents:
        raise ConfigurationError(
            "output root must not be the project root or its parent"
        )
    resolved, plugins = resolve_selection(config_path, project_root)
    if output_root.exists():
        shutil.rmtree(output_root)
    include_dir = output_root / "include"
    include_dir.mkdir(parents=True)
    (include_dir / "generated_features.h").write_text(generated_header(resolved), encoding="utf-8")
    data_dir = copy_assets(project_root, output_root, resolved, plugins)
    state = normalized_selection(resolved, plugins)
    state["runtime_littlefs"] = "littlefs" in resolved.features
    (output_root / "selection.json").write_text(
        json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return resolved, plugins, include_dir, data_dir


def comma_values(value):
    if not value.strip():
        return []
    items = [item.strip() for item in value.split(",")]
    if any(not item for item in items):
        raise ConfigurationError("comma-separated values contain an empty item")
    return items


def write_selection(path, firmware_ref, features, plugins):
    value = {
        "firmware_ref": firmware_ref,
        "features": comma_values(features),
        "plugins": comma_values(plugins),
    }
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_build_sizes(path):
    text = ANSI_ESCAPE.sub("", Path(path).read_text(encoding="utf-8", errors="replace"))
    patterns = {
        "static_ram_bytes": re.compile(
            r"RAM:\s+.*?used\s+(\d+)\s+bytes", re.IGNORECASE
        ),
        "firmware_flash_bytes": re.compile(
            r"Flash:\s+.*?used\s+(\d+)\s+bytes", re.IGNORECASE
        ),
    }
    sizes = {}
    for name, pattern in patterns.items():
        matches = pattern.findall(text)
        if not matches:
            raise ConfigurationError(f"cannot find {name} in PlatformIO build log: {path}")
        sizes[name] = int(matches[-1])
    return sizes


def write_manifest(
    config_path, project_root, build_dir, build_log, output, source_sha, environment
):
    resolved, plugins = resolve_selection(config_path, project_root)
    build_dir = Path(build_dir)
    names = ["firmware.bin", "bootloader.bin", "partitions.bin"]
    if (build_dir / "littlefs.bin").is_file():
        names.append("littlefs.bin")
    missing = [name for name in names[:3] if not (build_dir / name).is_file()]
    if missing:
        raise ConfigurationError(f"missing build artifacts: {missing}")
    artifacts = []
    for name in names:
        path = build_dir / name
        artifacts.append(
            {"name": name, "size_bytes": path.stat().st_size, "sha256": sha256(path)}
        )
    measured_sizes = parse_build_sizes(build_log)
    littlefs_path = build_dir / "littlefs.bin"
    if littlefs_path.is_file():
        measured_sizes["littlefs_bytes"] = littlefs_path.stat().st_size
    manifest = {
        "schema": 1,
        "custom_build": True,
        "source_sha": source_sha,
        "firmware_ref": resolved.firmware_ref,
        "environment": environment,
        "features": list(resolved.features),
        "plugins": [
            {"id": plugin_id, "version": plugins[plugin_id].version}
            for plugin_id in resolved.plugins
        ],
        "measured_sizes": measured_sizes,
        "artifacts": artifacts,
    }
    Path(output).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def platformio_entry(env):
    project_root = Path(env.subst("$PROJECT_DIR"))
    config_path = Path(
        os.environ.get(
            "HDS_CUSTOM_BUILD_CONFIG",
            env.GetProjectOption("custom_hds_config", "custom-build.json"),
        )
    )
    if not config_path.is_absolute():
        config_path = project_root / config_path
    environment = env.subst("$PIOENV")
    output_root = project_root / ".pio.nosync" / "generated" / environment
    resolved, _, include_dir, data_dir = prepare_build(config_path, project_root, output_root)
    env.Prepend(CPPPATH=[str(include_dir)])
    env.Replace(PROJECT_DATA_DIR=str(data_dir))
    ignored = []
    if "webserver" not in resolved.features:
        ignored.extend(["AsyncTCP", "ESPAsyncWebServer"])
    if "elegant_ota" not in resolved.features:
        ignored.append("ElegantOTA")
    if ignored:
        env.AppendUnique(LIB_IGNORE=ignored)
    print(
        "[custom-build] features={} plugins={} littlefs={}".format(
            ",".join(resolved.features) or "none",
            ",".join(resolved.plugins) or "none",
            "littlefs" in resolved.features,
        )
    )


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--config", required=True)
    validate_parser.add_argument("--project-root", default=".")

    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--config", required=True)
    prepare_parser.add_argument("--project-root", default=".")
    prepare_parser.add_argument("--output-root", required=True)

    selection_parser = subparsers.add_parser("selection")
    selection_parser.add_argument("--firmware-ref", required=True)
    selection_parser.add_argument("--features", default="")
    selection_parser.add_argument("--plugins", default="")
    selection_parser.add_argument("--output", required=True)

    manifest_parser = subparsers.add_parser("manifest")
    manifest_parser.add_argument("--config", required=True)
    manifest_parser.add_argument("--project-root", default=".")
    manifest_parser.add_argument("--build-dir", required=True)
    manifest_parser.add_argument("--build-log", required=True)
    manifest_parser.add_argument("--output", required=True)
    manifest_parser.add_argument("--source-sha", required=True)
    manifest_parser.add_argument("--environment", default="esp32s3-custom")

    args = parser.parse_args()
    try:
        if args.command == "selection":
            write_selection(args.output, args.firmware_ref, args.features, args.plugins)
        elif args.command == "validate":
            resolved, plugins = resolve_selection(args.config, args.project_root)
            print(json.dumps(normalized_selection(resolved, plugins), sort_keys=True))
        elif args.command == "prepare":
            resolved, plugins, _, _ = prepare_build(
                args.config, args.project_root, args.output_root
            )
            print(json.dumps(normalized_selection(resolved, plugins), sort_keys=True))
        elif args.command == "manifest":
            write_manifest(
                args.config,
                args.project_root,
                args.build_dir,
                args.build_log,
                args.output,
                args.source_sha,
                args.environment,
            )
    except ConfigurationError as error:
        parser.error(str(error))


if "Import" in globals():
    Import("env")
    platformio_entry(env)


if __name__ == "__main__":
    main()
