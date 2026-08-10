import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess


SCRIPT_ROOT = Path(globals().get("__file__", Path.cwd() / "tools" / "configure_custom_build.py")).resolve().parents[1]
ROOT = Path(os.environ.get("HDS_CUSTOM_BUILD_CATALOG_ROOT", SCRIPT_ROOT)).resolve()
ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
FIRMWARE_REFS = ("main",)
FEATURES = {
    "wifi": ("HDS_FEATURE_WIFI", ()),
    "mdns": ("HDS_FEATURE_MDNS", ("wifi",)),
    "webserver": ("HDS_FEATURE_WEBSERVER", ("wifi",)),
    "websocket": ("HDS_FEATURE_WEBSOCKET", ("wifi", "webserver")),
    "littlefs": ("HDS_FEATURE_LITTLEFS", ()),
    "elegant-ota": ("HDS_FEATURE_ELEGANT_OTA", ("wifi", "webserver")),
    "pull-ota": ("HDS_FEATURE_PULL_OTA", ("wifi",)),
    "grinder": ("HDS_FEATURE_GRINDER", ("wifi", "mdns")),
}
FEATURE_PRESENTATION = {
    "wifi": (
        "WiFi",
        "Wireless networking support for connected features.",
        "Provides the WiFi stack used by OTA updates, discovery, web tools, and connected plugins.",
    ),
    "mdns": (
        "mDNS",
        "Local network discovery without an IP address.",
        "Advertises the scale on the local network using multicast DNS. WiFi is included automatically.",
    ),
    "webserver": (
        "Web server",
        "Serve the local configuration interface.",
        "Adds the embedded HTTP server for web settings, APIs, and approved web plugins.",
    ),
    "websocket": (
        "WebSocket",
        "Bidirectional live updates for web clients.",
        "Enables persistent browser communication and includes WiFi and the web server.",
    ),
    "littlefs": (
        "Runtime LittleFS files",
        "Bundle files in the runtime flash filesystem.",
        "Adds runtime assets such as HTML, configuration, or plugin resources to LittleFS.",
    ),
    "elegant-ota": (
        "ElegantOTA",
        "Browser-based firmware update flow.",
        "Adds ElegantOTA and includes WiFi and the web server.",
    ),
    "pull-ota": (
        "OLED Pull OTA",
        "Install official releases from the scale display.",
        "Downloads signed firmware and LittleFS release assets over WiFi from the OLED update flow.",
    ),
    "grinder": (
        "Grinder",
        "Enable supported grinder integrations.",
        "Adds grinder connectivity and includes WiFi and mDNS.",
    ),
}
DEFAULT_FEATURES = {"pull-ota"}
CONFIG_KEYS = {"firmware_ref", "features", "plugins"}
PLUGIN_KEYS = {
    "schema", "id", "name", "description", "tooltip", "version",
    "firmware_refs", "requires", "conflicts", "patches", "assets", "budget",
}
BUDGET_KEYS = {"firmware_flash_bytes", "static_ram_bytes", "littlefs_bytes"}
BUILD_CONTRACT_SCHEMA = 1
PLATFORMIO_ENVIRONMENT = "esp32s3-custom"


def readJson(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid JSON: {path}: {error}") from error


def requireStringList(value, name):
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ValueError(f"{name} must be a string array")
    if len(value) != len(set(value)):
        raise ValueError(f"{name} contains duplicates")
    return value


def safeRelativePath(value, name):
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError(f"{name} must be a non-empty POSIX path")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise ValueError(f"{name} must be relative and cannot contain . or ..")
    return path


def loadPlugin(pluginId, firmwareRef=None):
    if not ID_PATTERN.fullmatch(pluginId):
        raise ValueError(f"invalid plugin id: {pluginId}")
    pluginDir = ROOT / "plugins" / pluginId
    manifest = readJson(pluginDir / "plugin.json")
    if not isinstance(manifest, dict) or set(manifest) != PLUGIN_KEYS:
        raise ValueError(f"invalid plugin manifest keys: {pluginId}")
    if manifest["schema"] != 2 or manifest["id"] != pluginId:
        raise ValueError(f"invalid plugin identity: {pluginId}")
    for field in ("name", "description", "tooltip", "version"):
        if not isinstance(manifest[field], str) or not manifest[field].strip():
            raise ValueError(f"invalid {field}: {pluginId}")
    firmwareRefs = requireStringList(manifest["firmware_refs"], f"{pluginId}.firmware_refs")
    unknownFirmwareRefs = set(firmwareRefs) - set(FIRMWARE_REFS)
    if not firmwareRefs or unknownFirmwareRefs:
        raise ValueError(f"invalid firmware refs for {pluginId}: {sorted(unknownFirmwareRefs)}")
    if firmwareRef is not None and firmwareRef not in firmwareRefs:
        raise ValueError(f"plugin {pluginId} does not support {firmwareRef}")
    requires = requireStringList(manifest["requires"], f"{pluginId}.requires")
    conflicts = requireStringList(manifest["conflicts"], f"{pluginId}.conflicts")
    unknownFeatures = set(requires) - set(FEATURES)
    if unknownFeatures:
        raise ValueError(f"unknown plugin features: {sorted(unknownFeatures)}")
    if any(not ID_PATTERN.fullmatch(conflict) for conflict in conflicts) or pluginId in conflicts:
        raise ValueError(f"invalid plugin conflicts: {pluginId}")
    budget = manifest["budget"]
    if not isinstance(budget, dict) or set(budget) != BUDGET_KEYS:
        raise ValueError(f"invalid plugin budget: {pluginId}")
    if any(type(budget[key]) is not int or budget[key] < 0 for key in BUDGET_KEYS):
        raise ValueError(f"invalid plugin budget values: {pluginId}")
    assets = manifest["assets"]
    if not isinstance(assets, list):
        raise ValueError(f"invalid plugin assets: {pluginId}")
    checkedAssets = []
    for asset in assets:
        if not isinstance(asset, dict) or set(asset) != {"source", "target"}:
            raise ValueError(f"invalid plugin asset: {pluginId}")
        sourceRelative = safeRelativePath(asset["source"], f"{pluginId}.source")
        targetRelative = safeRelativePath(asset["target"], f"{pluginId}.target")
        source = pluginDir.joinpath(*sourceRelative.parts).resolve()
        if pluginDir.resolve() not in source.parents or not source.is_file():
            raise ValueError(f"missing plugin asset: {pluginId}/{sourceRelative}")
        checkedAssets.append((source, targetRelative))
    assetTargets = [target.as_posix() for _, target in checkedAssets]
    if len(assetTargets) != len(set(assetTargets)):
        raise ValueError(f"duplicate plugin asset target: {pluginId}")
    patches = manifest["patches"]
    if not isinstance(patches, dict) or any(not isinstance(ref, str) for ref in patches):
        raise ValueError(f"invalid plugin patches: {pluginId}")
    unknownPatchRefs = set(patches) - set(firmwareRefs)
    if unknownPatchRefs:
        raise ValueError(f"patch targets unsupported firmware refs: {sorted(unknownPatchRefs)}")
    checkedPatches = {}
    for ref, value in patches.items():
        sourceRelative = safeRelativePath(value, f"{pluginId}.patches.{ref}")
        if sourceRelative.parts[0] != "patches" or sourceRelative.suffix != ".patch":
            raise ValueError(f"invalid plugin patch path: {pluginId}/{sourceRelative}")
        source = pluginDir.joinpath(*sourceRelative.parts).resolve()
        if pluginDir.resolve() not in source.parents or not source.is_file():
            raise ValueError(f"missing plugin patch: {pluginId}/{sourceRelative}")
        checkedPatches[ref] = source
    return manifest, checkedAssets, checkedPatches


def loadPluginCatalog():
    pluginIds = [path.parent.name for path in sorted((ROOT / "plugins").glob("*/plugin.json"))]
    plugins = {pluginId: loadPlugin(pluginId) for pluginId in pluginIds}
    knownIds = set(plugins)
    for pluginId, (manifest, _, _) in plugins.items():
        unknownConflicts = set(manifest["conflicts"]) - knownIds
        if unknownConflicts:
            raise ValueError(f"unknown plugin conflicts for {pluginId}: {sorted(unknownConflicts)}")
    return plugins


def buildBrowserCatalog():
    plugins = loadPluginCatalog()
    features = []
    for featureId, (_, dependencies) in FEATURES.items():
        name, description, tooltip = FEATURE_PRESENTATION[featureId]
        feature = {
            "id": featureId,
            "name": name,
            "description": description,
            "tooltip": tooltip,
            "requires": list(dependencies),
        }
        if featureId in DEFAULT_FEATURES:
            feature["default"] = True
        features.append(feature)
    return {
        "firmware_refs": list(FIRMWARE_REFS),
        "features": features,
        "plugins": [
            {
                key: manifest[key]
                for key in (
                    "id", "name", "description", "tooltip", "version",
                    "firmware_refs", "requires", "conflicts",
                )
            }
            for manifest, _, _ in plugins.values()
        ],
    }


def writeBrowserCatalog(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(buildBrowserCatalog(), indent=2) + "\n").encode("utf-8"))


def buildServiceCatalog(sourceRoot=ROOT):
    plugins = loadPluginCatalog()
    partition = partitionMetadata(sourceRoot)
    return {
        "schema": BUILD_CONTRACT_SCHEMA,
        "firmware_refs": list(FIRMWARE_REFS),
        "platformio_environment": PLATFORMIO_ENVIRONMENT,
        "partition_schema": {
            "path": partition["path"],
            "sha256": partition["sha256"],
        },
        "features": {
            featureId: list(dependencies)
            for featureId, (_, dependencies) in FEATURES.items()
        },
        "plugins": {
            pluginId: {
                "version": manifest["version"],
                "firmware_refs": manifest["firmware_refs"],
                "requires": manifest["requires"],
                "conflicts": manifest["conflicts"],
                "patches": {
                    firmwareRef: {"sha256": fileMetadata(path)["sha256"]}
                    for firmwareRef, path in sorted(patches.items())
                },
                "assets": sorted(
                    [
                        {
                            "target": target.as_posix(),
                            "sha256": fileMetadata(source)["sha256"],
                        }
                        for source, target in assets
                    ],
                    key=lambda asset: (asset["target"], asset["sha256"]),
                ),
            }
            for pluginId, (manifest, assets, patches) in plugins.items()
        },
    }


def writeServiceCatalog(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(buildServiceCatalog(), indent=2) + "\n").encode("utf-8"))


def resolveConfiguration(configPath):
    config = readJson(configPath)
    if not isinstance(config, dict) or set(config) != CONFIG_KEYS:
        raise ValueError("custom build must contain firmware_ref, features, and plugins")
    firmwareRef = config["firmware_ref"]
    if firmwareRef not in FIRMWARE_REFS:
        raise ValueError(f"unsupported firmware_ref: {firmwareRef}")
    requestedFeatures = requireStringList(config["features"], "features")
    unknownFeatures = set(requestedFeatures) - set(FEATURES)
    if unknownFeatures:
        raise ValueError(f"unknown features: {sorted(unknownFeatures)}")
    pluginIds = sorted(requireStringList(config["plugins"], "plugins"))
    pluginCatalog = loadPluginCatalog()
    unknownPlugins = set(pluginIds) - set(pluginCatalog)
    if unknownPlugins:
        raise ValueError(f"unknown plugins: {sorted(unknownPlugins)}")
    plugins = []
    assets = []
    patches = []
    resolved = set(requestedFeatures)
    for pluginId in pluginIds:
        manifest, pluginAssets, pluginPatches = pluginCatalog[pluginId]
        if firmwareRef not in manifest["firmware_refs"]:
            raise ValueError(f"plugin {pluginId} does not support {firmwareRef}")
        plugins.append(manifest)
        assets.extend(pluginAssets)
        if firmwareRef in pluginPatches:
            patches.append((pluginId, pluginPatches[firmwareRef]))
        resolved.update(manifest["requires"])
    changed = True
    while changed:
        previous = set(resolved)
        for feature in previous:
            resolved.update(FEATURES[feature][1])
        changed = resolved != previous
    selectedPlugins = set(pluginIds)
    for plugin in plugins:
        conflicts = selectedPlugins.intersection(plugin["conflicts"])
        if conflicts:
            raise ValueError(f"plugin {plugin['id']} conflicts with {sorted(conflicts)}")
    targets = [target.as_posix() for _, target in assets]
    if len(targets) != len(set(targets)):
        raise ValueError("plugin asset target collision")
    existingTargets = [target for target in targets if (ROOT / "web_apps" / target).exists()]
    if existingTargets:
        raise ValueError(f"plugin asset target collides with web asset: {existingTargets}")
    return {
        "firmware_ref": firmwareRef,
        "features": sorted(resolved),
        "plugins": plugins,
        "assets": assets,
        "patches": patches,
    }


def writeHeader(configuration, outputDir):
    outputDir.mkdir(parents=True, exist_ok=True)
    enabled = set(configuration["features"])
    lines = ["#ifndef HDS_GENERATED_FEATURES_H", "#define HDS_GENERATED_FEATURES_H", ""]
    for feature, (macro, _) in FEATURES.items():
        lines.append(f"#define {macro} {int(feature in enabled)}")
    lines.extend(("", "#endif", ""))
    (outputDir / "generated_features.h").write_text("\n".join(lines), encoding="ascii")


def stageAssets(configuration, stageDir, firmwareRoot=ROOT):
    targets = [target for _, target in configuration["assets"]]
    collisions = [target.as_posix() for target in targets if firmwareRoot.joinpath("web_apps", *target.parts).exists()]
    if collisions:
        raise ValueError(f"plugin asset target collides with selected firmware asset: {collisions}")
    if stageDir.exists():
        shutil.rmtree(stageDir)
    stageDir.mkdir(parents=True)
    if "littlefs" in configuration["features"]:
        shutil.copytree(firmwareRoot / "web_apps", stageDir, dirs_exist_ok=True)
    for source, target in configuration["assets"]:
        destination = stageDir.joinpath(*target.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def serializableConfiguration(configuration):
    return {
        "firmware_ref": configuration["firmware_ref"],
        "features": configuration["features"],
        "plugins": [
            {"id": plugin["id"], "version": plugin["version"]}
            for plugin in configuration["plugins"]
        ],
    }


def configure(configPath, workspaceDir, firmwareRoot=ROOT):
    configuration = resolveConfiguration(configPath)
    writeHeader(configuration, workspaceDir / "generated" / "include")
    stageAssets(configuration, workspaceDir / "custom-data", firmwareRoot)
    resolvedPath = workspaceDir / "custom-build-resolved.json"
    resolvedPath.parent.mkdir(parents=True, exist_ok=True)
    resolvedPath.write_text(
        json.dumps(serializableConfiguration(configuration), sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    return configuration


def fileMetadata(path):
    return {
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def packageMetadata(configuration):
    packages = []
    for plugin in configuration["plugins"]:
        pluginId = plugin["id"]
        pluginDir = (ROOT / "plugins" / pluginId).resolve()
        _, assets, patches = loadPlugin(pluginId, configuration["firmware_ref"])
        packages.append({
            "id": pluginId,
            "version": plugin["version"],
            "manifest": {
                "path": "plugin.json",
                **fileMetadata(pluginDir / "plugin.json"),
            },
            "patches": [
                {
                    "firmware_ref": firmwareRef,
                    "path": path.relative_to(pluginDir).as_posix(),
                    **fileMetadata(path),
                }
                for firmwareRef, path in sorted(patches.items())
            ],
            "assets": [
                {
                    "source": source.relative_to(pluginDir).as_posix(),
                    "target": target.as_posix(),
                    **fileMetadata(source),
                }
                for source, target in assets
            ],
        })
    return packages


def partitionMetadata(sourceRoot):
    config = configparser.ConfigParser(interpolation=None)
    if not config.read(sourceRoot / "platformio.ini", encoding="utf-8"):
        raise ValueError("selected firmware has no platformio.ini")
    section = "env:esp32s3-custom"
    visited = set()
    while section not in visited and config.has_section(section):
        visited.add(section)
        if config.has_option(section, "board_build.partitions"):
            relative = safeRelativePath(config.get(section, "board_build.partitions").strip(), "partition schema")
            path = sourceRoot.joinpath(*relative.parts).resolve()
            if sourceRoot.resolve() not in path.parents or not path.is_file():
                raise ValueError(f"missing partition schema: {relative}")
            return {"path": relative.as_posix(), **fileMetadata(path)}
        parent = config.get(section, "extends", fallback="").strip()
        section = parent if parent.startswith("env:") else f"env:{parent}"
    raise ValueError("esp32s3-custom has no partition schema")


def combinationInput(configuration, commitSha, sourceRoot=ROOT):
    packages = []
    for package in packageMetadata(configuration):
        packages.append({
            "id": package["id"],
            "version": package["version"],
            "patches": sorted(
                [
                    {"sha256": patch["sha256"]}
                    for patch in package["patches"]
                    if patch["firmware_ref"] == configuration["firmware_ref"]
                ],
                key=lambda patch: patch["sha256"],
            ),
            "assets": sorted(
                [
                    {
                        "target": asset["target"],
                        "sha256": asset["sha256"],
                    }
                    for asset in package["assets"]
                ],
                key=lambda asset: (asset["target"], asset["sha256"]),
            ),
        })
    partition = partitionMetadata(sourceRoot)
    return {
        "schema": BUILD_CONTRACT_SCHEMA,
        "base_source": commitSha,
        "features": sorted(configuration["features"]),
        "plugins": sorted(packages, key=lambda package: package["id"]),
        "platformio_environment": PLATFORMIO_ENVIRONMENT,
        "partition_schema": {
            "path": partition["path"],
            "sha256": partition["sha256"],
        },
    }


def combinationHash(value):
    canonical = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def writeBuildManifest(
    configuration, buildDir, outputPath, commitSha=None, sourceRoot=ROOT, identity=None
):
    if commitSha is None:
        commitSha = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
            capture_output=True, text=True,
        ).stdout.strip()
    if identity is None:
        identity = combinationInput(configuration, commitSha, sourceRoot)
    binaries = {}
    for path in sorted(buildDir.glob("*.bin")):
        binaries[path.name] = fileMetadata(path)
    manifest = {
        **serializableConfiguration(configuration),
        "base_source": commitSha,
        "platformio_environment": PLATFORMIO_ENVIRONMENT,
        "partition_schema": partitionMetadata(sourceRoot),
        "packages": packageMetadata(configuration),
        "combination_input": identity,
        "combination_hash": combinationHash(identity),
        "custom_build": True,
        "binaries": binaries,
        "dependencies": fileMetadata(buildDir / "dependencies.txt"),
    }
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    outputPath.write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return manifest["combination_hash"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=ROOT / "custom-build.json")
    parser.add_argument("--workspace", type=Path, default=ROOT / ".pio.nosync")
    parser.add_argument("--manifest-build-dir", type=Path)
    parser.add_argument("--manifest-output", type=Path)
    parser.add_argument("--commit-sha")
    parser.add_argument("--catalog-output", type=Path)
    parser.add_argument("--service-catalog-output", type=Path)
    args = parser.parse_args()
    configuration = configure(args.config.resolve(), args.workspace.resolve())
    if bool(args.manifest_build_dir) != bool(args.manifest_output):
        parser.error("manifest build directory and output must be provided together")
    if args.manifest_build_dir:
        writeBuildManifest(
            configuration,
            args.manifest_build_dir.resolve(),
            args.manifest_output.resolve(),
            args.commit_sha,
        )
    if args.catalog_output:
        writeBrowserCatalog(args.catalog_output.resolve())
    if args.service_catalog_output:
        writeServiceCatalog(args.service_catalog_output.resolve())
    print(json.dumps(serializableConfiguration(configuration), sort_keys=True))


try:
    Import("env")
except NameError:
    if __name__ == "__main__":
        main()
else:
    if not env.IsCleanTarget():
        projectRoot = Path(env.subst("$PROJECT_DIR")).resolve()
        workspace = projectRoot / ".pio.nosync"
        configPath = Path(os.environ.get("HDS_CUSTOM_BUILD_CONFIG", ROOT / "custom-build.json")).resolve()
        configure(configPath, workspace, projectRoot)
        env.Replace(PROJECT_DATA_DIR=str(workspace / "custom-data"))
