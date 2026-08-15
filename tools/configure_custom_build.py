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
PATH_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/-]*$")
STABLE_FIRMWARE_REF_PATTERN = re.compile(r"^v?([0-9]+\.[0-9]+\.[0-9]+)$")
DEFAULT_FIRMWARE_VERSION_PATTERN = re.compile(
    r'^\s*#define\s+HDS_FIRMWARE_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9]+(?:-[a-z0-9]+)*)?)"\s*$',
    re.MULTILINE,
)
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
        "Wireless networking with inline configuration support.",
        "Provides WiFi and a minimal setup server for network and device-name configuration.",
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
        "Wifi Pull OTA",
        "Install official releases from the scale display. Updates replace custom builds.",
        "Downloads signed firmware and LittleFS release assets over WiFi. Installing an official release replaces the custom build.",
    ),
    "grinder": (
        "Grind by weight core",
        "Enable the built-in grinder integration.",
        "Internal compile gate selected by the Grind by weight plugin.",
    ),
}
HIDDEN_FEATURES = {"grinder"}
DEFAULT_FEATURES = set(FEATURES) - HIDDEN_FEATURES
DEFAULT_PLUGINS = {"default-web-apps"}
CONFIG_KEYS = {"firmware_ref", "features", "plugins"}
PLUGIN_KEYS = {
    "schema", "id", "name", "description", "tooltip", "version",
    "firmware_refs", "requires", "conflicts", "patches", "assets", "budget",
}
OPTIONAL_PLUGIN_KEYS = {"depends_on", "recommends"}
RECOMMENDATION_KEYS = {"features", "plugins"}
BUDGET_KEYS = {"firmware_flash_bytes", "static_ram_bytes", "littlefs_bytes"}
BUILD_CONTRACT_SCHEMA = 2
PLATFORMIO_ENVIRONMENT = "esp32s3-custom"
PUBLIC_BINARIES = ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin")
FIRMWARE_ARCHIVE = "HDS_FW_custom.zip"


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
    if not isinstance(value, str) or not PATH_PATTERN.fullmatch(value) or "\\" in value:
        raise ValueError(f"{name} must be a non-empty POSIX path")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise ValueError(f"{name} must be relative and cannot contain . or ..")
    return path


def loadPlugin(pluginId, firmwareRef=None):
    if not ID_PATTERN.fullmatch(pluginId):
        raise ValueError(f"invalid plugin id: {pluginId}")
    pluginDir = ROOT / "plugins" / pluginId
    storedManifest = readJson(pluginDir / "plugin.json")
    if (not isinstance(storedManifest, dict) or
            not PLUGIN_KEYS.issubset(storedManifest) or
            set(storedManifest) - PLUGIN_KEYS - OPTIONAL_PLUGIN_KEYS):
        raise ValueError(f"invalid plugin manifest keys: {pluginId}")
    manifest = {
        **storedManifest,
        "depends_on": storedManifest.get("depends_on", []),
        "recommends": storedManifest.get("recommends", {"features": [], "plugins": []}),
    }
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
    dependsOn = requireStringList(manifest["depends_on"], f"{pluginId}.depends_on")
    conflicts = requireStringList(manifest["conflicts"], f"{pluginId}.conflicts")
    unknownFeatures = set(requires) - set(FEATURES)
    if unknownFeatures:
        raise ValueError(f"unknown plugin features: {sorted(unknownFeatures)}")
    if any(not ID_PATTERN.fullmatch(dependency) for dependency in dependsOn) or pluginId in dependsOn:
        raise ValueError(f"invalid plugin dependencies: {pluginId}")
    if any(not ID_PATTERN.fullmatch(conflict) for conflict in conflicts) or pluginId in conflicts:
        raise ValueError(f"invalid plugin conflicts: {pluginId}")
    recommendations = manifest["recommends"]
    if not isinstance(recommendations, dict) or set(recommendations) != RECOMMENDATION_KEYS:
        raise ValueError(f"invalid plugin recommendations: {pluginId}")
    recommendedFeatures = requireStringList(
        recommendations["features"], f"{pluginId}.recommends.features"
    )
    recommendedPlugins = requireStringList(
        recommendations["plugins"], f"{pluginId}.recommends.plugins"
    )
    unknownRecommendedFeatures = set(recommendedFeatures) - set(FEATURES)
    if unknownRecommendedFeatures:
        raise ValueError(f"unknown recommended features: {sorted(unknownRecommendedFeatures)}")
    if (any(not ID_PATTERN.fullmatch(recommended) for recommended in recommendedPlugins) or
            pluginId in recommendedPlugins):
        raise ValueError(f"invalid recommended plugins: {pluginId}")
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


def pluginOrder(pluginCatalog, requestedIds):
    closure = set()
    pending = list(requestedIds)
    while pending:
        pluginId = pending.pop()
        if pluginId in closure:
            continue
        if pluginId not in pluginCatalog:
            raise ValueError(f"unknown plugin dependency: {pluginId}")
        closure.add(pluginId)
        pending.extend(pluginCatalog[pluginId][0]["depends_on"])

    ordered = []
    visiting = set()
    visited = set()

    def visit(pluginId):
        if pluginId in visiting:
            raise ValueError(f"plugin dependency cycle: {pluginId}")
        if pluginId in visited:
            return
        if pluginId not in pluginCatalog:
            raise ValueError(f"unknown plugin dependency: {pluginId}")
        visiting.add(pluginId)
        for dependency in sorted(pluginCatalog[pluginId][0]["depends_on"]):
            visit(dependency)
        visiting.remove(pluginId)
        visited.add(pluginId)
        ordered.append(pluginId)

    for pluginId in sorted(closure):
        visit(pluginId)
    return ordered


def loadPluginCatalog():
    pluginIds = [path.parent.name for path in sorted((ROOT / "plugins").glob("*/plugin.json"))]
    plugins = {pluginId: loadPlugin(pluginId) for pluginId in pluginIds}
    knownIds = set(plugins)
    for pluginId, (manifest, _, _) in plugins.items():
        for field in ("conflicts", "depends_on"):
            unknownIds = set(manifest[field]) - knownIds
            if unknownIds:
                raise ValueError(f"unknown plugin {field} for {pluginId}: {sorted(unknownIds)}")
        for dependencyId in manifest["depends_on"]:
            dependencyRefs = set(plugins[dependencyId][0]["firmware_refs"])
            unsupportedRefs = set(manifest["firmware_refs"]) - dependencyRefs
            if unsupportedRefs:
                raise ValueError(
                    f"plugin dependency {dependencyId} does not support {sorted(unsupportedRefs)}"
                )
        unknownRecommendations = set(manifest["recommends"]["plugins"]) - knownIds
        if unknownRecommendations:
            raise ValueError(
                f"unknown recommended plugins for {pluginId}: {sorted(unknownRecommendations)}"
            )
        for recommendedId in manifest["recommends"]["plugins"]:
            recommendedRefs = set(plugins[recommendedId][0]["firmware_refs"])
            unsupportedRefs = set(manifest["firmware_refs"]) - recommendedRefs
            if unsupportedRefs:
                raise ValueError(
                    f"recommended plugin {recommendedId} does not support {sorted(unsupportedRefs)}"
                )
    pluginOrder(plugins, knownIds)
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
        if featureId in HIDDEN_FEATURES:
            feature["hidden"] = True
        features.append(feature)
    return {
        "firmware_refs": list(FIRMWARE_REFS),
        "features": features,
        "plugins": [
            {
                **{
                    key: manifest[key]
                    for key in (
                        "id", "name", "description", "tooltip", "version",
                        "firmware_refs", "requires", "depends_on", "conflicts", "recommends",
                    )
                },
                **({"default": True} if manifest["id"] in DEFAULT_PLUGINS else {}),
            }
            for manifest, _, _ in plugins.values()
        ],
    }


def writeBrowserCatalog(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(buildBrowserCatalog(), indent=2) + "\n").encode("utf-8"))


def buildServiceCatalog(sourceRoot=ROOT):
    plugins = loadPluginCatalog()
    return {
        "schema": BUILD_CONTRACT_SCHEMA,
        "firmware_refs": list(FIRMWARE_REFS),
        "platformio_environment": PLATFORMIO_ENVIRONMENT,
        "firmware": {
            firmwareRef: firmwareMetadata(firmwareRef, sourceRoot)
            for firmwareRef in FIRMWARE_REFS
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
                "depends_on": manifest["depends_on"],
                "conflicts": manifest["conflicts"],
                "recommends": manifest["recommends"],
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
    requestedPluginIds = requireStringList(config["plugins"], "plugins")
    pluginCatalog = loadPluginCatalog()
    unknownPlugins = set(requestedPluginIds) - set(pluginCatalog)
    if unknownPlugins:
        raise ValueError(f"unknown plugins: {sorted(unknownPlugins)}")
    pluginIds = pluginOrder(pluginCatalog, requestedPluginIds)
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
    return {
        "firmware_ref": firmwareRef,
        "requested_plugins": sorted(requestedPluginIds),
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


def stageAssets(configuration, stageDir):
    if stageDir.exists():
        shutil.rmtree(stageDir)
    stageDir.mkdir(parents=True)
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


def configure(configPath, workspaceDir):
    configuration = resolveConfiguration(configPath)
    writeHeader(configuration, workspaceDir / "generated" / "include")
    stageAssets(configuration, workspaceDir / "custom-data")
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


def bytesMetadata(payload):
    return {
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def firmwareFileBytes(firmwareRef, relativePath, sourceRoot=ROOT):
    relative = safeRelativePath(relativePath, "firmware path")
    if sourceRoot.resolve() != ROOT or firmwareRef == "main":
        path = sourceRoot.joinpath(*relative.parts).resolve()
        if sourceRoot.resolve() not in path.parents or not path.is_file():
            raise ValueError(f"selected firmware is missing {relative}")
        return path.read_bytes()
    try:
        return subprocess.run(
            ["git", "show", f"refs/tags/{firmwareRef}:{relative.as_posix()}"],
            cwd=ROOT,
            check=True,
            capture_output=True,
        ).stdout
    except subprocess.CalledProcessError as error:
        raise ValueError(f"cannot read {relative} from {firmwareRef}") from error


def customFirmwareVersion(firmwareRef, configHeader):
    stable = STABLE_FIRMWARE_REF_PATTERN.fullmatch(firmwareRef)
    if stable:
        return f"{stable.group(1)}-custom"
    match = DEFAULT_FIRMWARE_VERSION_PATTERN.search(configHeader)
    if not match:
        raise ValueError("selected firmware has no valid HDS_FIRMWARE_VERSION")
    return f"{match.group(1)}-custom"


def partitionMetadataFromBytes(platformioBytes, readPartition):
    config = configparser.ConfigParser(interpolation=None)
    try:
        config.read_string(platformioBytes.decode("utf-8"))
    except (UnicodeDecodeError, configparser.Error) as error:
        raise ValueError("selected firmware has an invalid platformio.ini") from error
    section = "env:esp32s3-custom"
    visited = set()
    while section not in visited and config.has_section(section):
        visited.add(section)
        if config.has_option(section, "board_build.partitions"):
            relative = safeRelativePath(
                config.get(section, "board_build.partitions").strip(), "partition schema"
            )
            payload = readPartition(relative.as_posix())
            return {"path": relative.as_posix(), **bytesMetadata(payload)}
        parent = config.get(section, "extends", fallback="").strip()
        section = parent if parent.startswith("env:") else f"env:{parent}"
    raise ValueError("esp32s3-custom has no partition schema")


def firmwareMetadata(firmwareRef, sourceRoot=ROOT):
    partition = partitionMetadataFromBytes(
        firmwareFileBytes(firmwareRef, "platformio.ini", sourceRoot),
        lambda path: firmwareFileBytes(firmwareRef, path, sourceRoot),
    )
    configHeader = firmwareFileBytes(firmwareRef, "include/config.h", sourceRoot).decode("utf-8")
    return {
        "custom_version": customFirmwareVersion(firmwareRef, configHeader),
        "partition_schema": {
            "path": partition["path"],
            "sha256": partition["sha256"],
        },
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
    return partitionMetadataFromBytes(
        (sourceRoot / "platformio.ini").read_bytes(),
        lambda path: sourceRoot.joinpath(*PurePosixPath(path).parts).read_bytes(),
    )


def combinationInput(configuration, commitSha, sourceRoot=ROOT, builderCommit=None):
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
    firmware = firmwareMetadata(configuration["firmware_ref"], sourceRoot)
    return {
        "schema": BUILD_CONTRACT_SCHEMA,
        "firmware_ref": configuration["firmware_ref"],
        "firmware_version": firmware["custom_version"],
        "base_source": commitSha,
        "builder_source": builderCommit or commitSha,
        "features": sorted(configuration["features"]),
        "plugins": packages,
        "platformio_environment": PLATFORMIO_ENVIRONMENT,
        "partition_schema": firmware["partition_schema"],
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
    binaries = {
        name: fileMetadata(buildDir / name)
        for name in PUBLIC_BINARIES
    }
    manifest = {
        **serializableConfiguration(configuration),
        "base_source": commitSha,
        "builder_source": identity["builder_source"],
        "firmware_version": identity["firmware_version"],
        "platformio_environment": PLATFORMIO_ENVIRONMENT,
        "partition_schema": partitionMetadata(sourceRoot),
        "packages": packageMetadata(configuration),
        "combination_input": identity,
        "combination_hash": combinationHash(identity),
        "custom_build": True,
        "binaries": binaries,
        "archive": fileMetadata(buildDir / FIRMWARE_ARCHIVE),
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
        workspace = Path(env.subst("$PROJECT_DIR")).resolve() / ".pio.nosync"
        configPath = Path(os.environ.get("HDS_CUSTOM_BUILD_CONFIG", ROOT / "custom-build.json")).resolve()
        configure(configPath, workspace)
        env.Replace(PROJECT_DATA_DIR=str(workspace / "custom-data"))
