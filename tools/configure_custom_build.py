#!/usr/bin/env python3

import hashlib
import json
import re
import shutil
from pathlib import Path

FEATURE_DEPENDENCIES = {
    "wifi": {"webserver"},
    "mdns": {"wifi"},
    "webserver": {"wifi"},
    "websocket": {"wifi", "webserver"},
    "littlefs": set(),
    "elegant-ota": {"wifi", "webserver"},
    "pull-ota": {"wifi"},
    "grinder": {"wifi", "mdns"},
}
FEATURE_MACROS = {
    "wifi": "HDS_FEATURE_WIFI",
    "mdns": "HDS_FEATURE_MDNS",
    "webserver": "HDS_FEATURE_WEBSERVER",
    "websocket": "HDS_FEATURE_WEBSOCKET",
    "littlefs": "HDS_FEATURE_LITTLEFS",
    "elegant-ota": "HDS_FEATURE_ELEGANT_OTA",
    "pull-ota": "HDS_FEATURE_PULL_OTA",
    "grinder": "HDS_FEATURE_GRINDER",
}
SUPPORTED_FIRMWARE_REFS = {"main"}
SAFE_ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")


class ConfigurationError(ValueError):
    pass


def read_json(path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigurationError(f"invalid JSON file {path}: {error}") from error


def safe_relative_path(value, label):
    if not isinstance(value, str) or not value:
        raise ConfigurationError(f"{label} must be a non-empty string")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise ConfigurationError(f"{label} must be a safe relative path: {value}")
    return path


def validate_id(value, label):
    if not isinstance(value, str) or not SAFE_ID.fullmatch(value):
        raise ConfigurationError(f"invalid {label}: {value!r}")
    return value


def load_plugin_catalog(repo_root):
    plugins_root = repo_root / "plugins"
    catalog = {}
    if not plugins_root.is_dir():
        return catalog
    for manifest_path in sorted(plugins_root.glob("*/plugin.json")):
        manifest = read_json(manifest_path)
        required = {
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
        if set(manifest) != required:
            raise ConfigurationError(f"unexpected plugin.json keys in {manifest_path}")
        plugin_id = validate_id(manifest["id"], "plugin id")
        if manifest_path.parent.name != plugin_id:
            raise ConfigurationError(f"plugin directory does not match id {plugin_id}")
        if plugin_id in catalog:
            raise ConfigurationError(f"duplicate plugin id: {plugin_id}")
        if manifest["schema"] != 1:
            raise ConfigurationError(f"unsupported plugin schema for {plugin_id}")
        if not isinstance(manifest["firmware_refs"], list) or not manifest["firmware_refs"]:
            raise ConfigurationError(f"firmware_refs must be a non-empty list for {plugin_id}")
        for firmware_ref in manifest["firmware_refs"]:
            if firmware_ref not in SUPPORTED_FIRMWARE_REFS:
                raise ConfigurationError(f"unsupported firmware ref in {plugin_id}: {firmware_ref}")
        for field in ("requires", "conflicts"):
            if not isinstance(manifest[field], list):
                raise ConfigurationError(f"{field} must be a list for {plugin_id}")
            for feature in manifest[field]:
                if feature not in FEATURE_DEPENDENCIES:
                    raise ConfigurationError(f"unknown feature {feature} in {plugin_id}")
        if not isinstance(manifest["assets"], list):
            raise ConfigurationError(f"assets must be a list for {plugin_id}")
        catalog[plugin_id] = (manifest, manifest_path.parent)
    return catalog


def resolve_features(selected, plugins):
    resolved = set(selected)
    resolved.add("pull-ota")
    for feature in resolved:
        if feature not in FEATURE_DEPENDENCIES:
            raise ConfigurationError(f"unknown feature: {feature}")
    for manifest, _ in plugins:
        resolved.update(manifest["requires"])
    changed = True
    while changed:
        changed = False
        for feature in tuple(sorted(resolved)):
            dependencies = FEATURE_DEPENDENCIES[feature]
            missing = dependencies - resolved
            if missing:
                resolved.update(missing)
                changed = True
    return resolved


def resolve_configuration(repo_root, config_path):
    config = read_json(config_path)
    if set(config) != {"firmware_ref", "features", "plugins"}:
        raise ConfigurationError("custom-build.json must contain firmware_ref, features, and plugins only")
    firmware_ref = config["firmware_ref"]
    if firmware_ref not in SUPPORTED_FIRMWARE_REFS:
        raise ConfigurationError(f"unsupported firmware_ref: {firmware_ref}")
    if not isinstance(config["features"], list) or not isinstance(config["plugins"], list):
        raise ConfigurationError("features and plugins must be lists")
    if len(config["features"]) != len(set(config["features"])):
        raise ConfigurationError("duplicate feature id")
    if len(config["plugins"]) != len(set(config["plugins"])):
        raise ConfigurationError("duplicate plugin id")

    catalog = load_plugin_catalog(repo_root)
    selected_plugins = []
    for plugin_id in config["plugins"]:
        validate_id(plugin_id, "plugin id")
        if plugin_id not in catalog:
            raise ConfigurationError(f"unknown plugin id: {plugin_id}")
        manifest, plugin_root = catalog[plugin_id]
        if firmware_ref not in manifest["firmware_refs"]:
            raise ConfigurationError(f"plugin {plugin_id} does not support {firmware_ref}")
        selected_plugins.append((manifest, plugin_root))

    resolved = resolve_features(config["features"], selected_plugins)
    for manifest, _ in selected_plugins:
        conflicts = set(manifest["conflicts"]) & resolved
        if conflicts:
            raise ConfigurationError(
                f"plugin {manifest['id']} conflicts with: {', '.join(sorted(conflicts))}"
            )
        if not set(manifest["requires"]).issubset(resolved):
            raise ConfigurationError(f"unresolved requirements for {manifest['id']}")
    if selected_plugins and "littlefs" not in resolved:
        raise ConfigurationError("plugins with web assets require littlefs")

    return {
        "firmware_ref": firmware_ref,
        "features": sorted(resolved),
        "plugins": [manifest for manifest, _ in selected_plugins],
        "plugin_roots": {manifest["id"]: root for manifest, root in selected_plugins},
    }


def write_generated_header(output_path, resolved):
    lines = ["#ifndef HDS_GENERATED_FEATURES_H", "#define HDS_GENERATED_FEATURES_H", ""]
    enabled = set(resolved["features"])
    for feature, macro in FEATURE_MACROS.items():
        lines.append(f"#define {macro} {1 if feature in enabled else 0}")
    lines.extend(["", "#define HDS_CUSTOM_BUILD_ACTIVE 1", "", "#endif", ""])
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def copy_file(source, target, seen_targets):
    relative_target = safe_relative_path(target, "asset target")
    target_key = relative_target.as_posix()
    if target_key in seen_targets:
        raise ConfigurationError(f"duplicate asset target: {target_key}")
    seen_targets.add(target_key)
    destination = seen_targets.root / relative_target
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


class TargetSet(set):
    root = None


def prepare_data_dir(repo_root, output_dir, resolved):
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)
    seen_targets = TargetSet()
    seen_targets.root = output_dir

    if "littlefs" in resolved["features"]:
        web_root = repo_root / "web_apps"
        for source in sorted(path for path in web_root.rglob("*") if path.is_file()):
            relative = source.relative_to(web_root)
            copy_file(source, relative, seen_targets)

    for manifest in resolved["plugins"]:
        plugin_root = resolved["plugin_roots"][manifest["id"]]
        for asset in manifest["assets"]:
            if set(asset) != {"source", "target"}:
                raise ConfigurationError(f"invalid asset entry in {manifest['id']}")
            source_relative = safe_relative_path(asset["source"], "asset source")
            source = plugin_root / source_relative
            if not source.is_file() or plugin_root not in source.resolve().parents:
                raise ConfigurationError(f"missing or unsafe asset source: {source_relative}")
            copy_file(source, asset["target"], seen_targets)


def write_resolved_manifest(path, resolved, config_path):
    config_bytes = config_path.read_bytes()
    payload = {
        "custom_build": True,
        "firmware_ref": resolved["firmware_ref"],
        "features": resolved["features"],
        "plugins": [
            {"id": plugin["id"], "name": plugin["name"], "version": plugin["version"]}
            for plugin in resolved["plugins"]
        ],
        "configuration_sha256": hashlib.sha256(config_bytes).hexdigest(),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")


def configure(repo_root, config_path):
    resolved = resolve_configuration(repo_root, config_path)
    build_root = repo_root / ".pio.nosync" / "custom-build"
    write_generated_header(repo_root / ".pio.nosync" / "generated" / "include" / "generated_features.h", resolved)
    prepare_data_dir(repo_root, build_root / "data", resolved)
    write_resolved_manifest(build_root / "resolved.json", resolved, config_path)
    return resolved, build_root / "data"


def configure_platformio():
    try:
        Import("env")
    except NameError:
        return
    if env.get("PIOENV") != "esp32s3-custom":
        return
    repo_root = Path(env.subst("$PROJECT_DIR")).resolve()
    config_path = repo_root / env.GetProjectOption("custom_build_config", "custom-build.json")
    _, data_dir = configure(repo_root, config_path)
    env.Replace(PROJECT_DATA_DIR=str(data_dir))


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    resolved_config, data_directory = configure(root, root / "custom-build.json")
    print(json.dumps({
        "features": resolved_config["features"],
        "plugins": [plugin["id"] for plugin in resolved_config["plugins"]],
        "data_dir": str(data_directory.relative_to(root)),
    }, sort_keys=True))
else:
    configure_platformio()
