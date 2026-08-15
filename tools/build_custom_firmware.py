import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile

import configure_custom_build as customBuild


ROOT = Path(__file__).resolve().parents[1]
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
HASH_PATTERN = re.compile(r"^[0-9a-f]{64}$")
BUILD_FILES = (
    customBuild.FIRMWARE_ARCHIVE,
    "build-manifest.json",
    "dependencies.txt",
)
MAX_REMOTE_MANIFEST_BYTES = 1024 * 1024


def runCommand(command, cwd, capture=False, environment=None):
    return subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        check=True,
        capture_output=capture,
        text=capture,
    )


def verifiedCommit(repositoryRoot, revision):
    result = runCommand(
        ["git", "rev-parse", "--verify", "--end-of-options", f"{revision}^{{commit}}"],
        repositoryRoot,
        capture=True,
    ).stdout.strip()
    if not SHA_PATTERN.fullmatch(result):
        raise ValueError(f"invalid resolved commit: {result}")
    return result


def resolveFirmwareCommit(repositoryRoot, firmwareRef):
    if firmwareRef not in customBuild.FIRMWARE_REFS:
        raise ValueError(f"unsupported firmware_ref: {firmwareRef}")
    revisions = (
        ("refs/remotes/origin/main", "refs/heads/main")
        if firmwareRef == "main"
        else (f"refs/tags/{firmwareRef}",)
    )
    for revision in revisions:
        try:
            return verifiedCommit(repositoryRoot, revision)
        except subprocess.CalledProcessError:
            pass
    raise ValueError(f"cannot resolve allowed firmware ref: {firmwareRef}")


def verifySourceCommit(repositoryRoot, sourceCommit):
    if not SHA_PATTERN.fullmatch(sourceCommit):
        raise ValueError("source commit must be a full lowercase SHA-1")
    return verifiedCommit(repositoryRoot, sourceCommit)


def verifyBuilderCommit(repositoryRoot, expectedCommit=None):
    actualCommit = verifiedCommit(repositoryRoot, "HEAD")
    if expectedCommit:
        if not SHA_PATTERN.fullmatch(expectedCommit):
            raise ValueError("builder commit must be a full lowercase SHA-1")
        if actualCommit != expectedCommit:
            raise ValueError("builder checkout does not match the expected commit")
    return actualCommit


def pullRequestCommit():
    if os.environ.get("GITHUB_EVENT_NAME") != "pull_request":
        return None
    sourceCommit = os.environ.get("GITHUB_SHA")
    if not sourceCommit:
        raise ValueError("pull request build has no GITHUB_SHA")
    return sourceCommit


def requestedSourceCommit(sourceCommit, verifyPluginEnvironment):
    return sourceCommit or (None if verifyPluginEnvironment else pullRequestCommit())


def cloneSource(repositoryRoot, commitSha, checkoutRoot):
    checkoutRoot.mkdir()
    runCommand(["git", "init", "--quiet"], checkoutRoot)
    runCommand(
        ["git", "fetch", "--no-tags", "--depth=1", str(repositoryRoot), commitSha],
        checkoutRoot,
    )
    runCommand(["git", "checkout", "--detach", "FETCH_HEAD"], checkoutRoot)


def applyPatches(configuration, checkoutRoot):
    for pluginId, patchPath in configuration["patches"]:
        try:
            runCommand(["git", "apply", "--check", "--whitespace=error", str(patchPath)], checkoutRoot)
            runCommand(["git", "apply", "--whitespace=error", str(patchPath)], checkoutRoot)
        except subprocess.CalledProcessError as error:
            raise ValueError(f"patch failed for plugin {pluginId}: {patchPath.name}") from error


def sourceDateEpoch(checkoutRoot):
    return runCommand(
        ["git", "show", "-s", "--format=%ct", "HEAD"], checkoutRoot, capture=True
    ).stdout.strip()


def replaceRequired(value, source, replacement, name):
    if source not in value:
        raise ValueError(f"selected firmware has incompatible {name}")
    return value.replace(source, replacement)


def prepareBuildCheckout(checkoutRoot, builderRoot=None):
    builderRoot = (builderRoot or ROOT).resolve()
    workspace = checkoutRoot / ".pio.nosync"
    workspace.mkdir(parents=True, exist_ok=True)
    builderTools = workspace / "builder-tools"
    builderTools.mkdir()
    shutil.copy2(builderRoot / "tools" / "configure_custom_build.py", builderTools)
    shutil.copy2(builderRoot / "git_rev_macro.py", builderTools)
    compilerScript = workspace / "reproducible_build.py"
    compilerScript.write_text(
        "from pathlib import Path\n"
        "Import(\"env\")\n"
        "projectDir = Path(env.subst(\"$PROJECT_DIR\")).resolve().as_posix()\n"
        "env.Append(CCFLAGS=[f\"-ffile-prefix-map={projectDir}=.\"])\n"
        "env.Append(LINKFLAGS=[\"-Wl,-s\"])\n",
        encoding="utf-8",
    )
    filesystemScript = workspace / "reproducible_filesystem.py"
    filesystemScript.write_text(
        "import os\n"
        "from pathlib import Path\n"
        "Import(\"env\")\n"
        "def normalizeTimestamps(source, target, env):\n"
        "    dataDir = Path(env.subst(\"$PROJECT_DATA_DIR\"))\n"
        "    epoch = int(os.environ[\"SOURCE_DATE_EPOCH\"])\n"
        "    paths = sorted(dataDir.rglob(\"*\"), key=lambda path: len(path.parts), reverse=True)\n"
        "    for path in [*paths, dataDir]:\n"
        "        os.utime(path, (epoch, epoch))\n"
        "env.AddPreAction(\"$BUILD_DIR/littlefs.bin\", normalizeTimestamps)\n",
        encoding="utf-8",
    )
    configPath = checkoutRoot / "platformio.ini"
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    if not config.read(configPath, encoding="utf-8"):
        raise ValueError("selected firmware has no platformio.ini")
    section = "env:esp32s3-custom"
    scripts = config.get(section, "extra_scripts", fallback="")
    scripts = replaceRequired(
        scripts,
        "pre:tools/configure_custom_build.py",
        "pre:.pio.nosync/builder-tools/configure_custom_build.py",
        "custom build configurator",
    )
    buildFlags = config.get("env:esp32s3", "build_flags", fallback="")
    config.set(
        "env:esp32s3",
        "build_flags",
        replaceRequired(
            buildFlags,
            "!python3 git_rev_macro.py",
            "!python3 .pio.nosync/builder-tools/git_rev_macro.py",
            "firmware version generator",
        ),
    )
    config.set(
        section,
        "extra_scripts",
        scripts
        + "\npre:.pio.nosync/reproducible_build.py"
        + "\npost:.pio.nosync/reproducible_filesystem.py",
    )
    with configPath.open("w", encoding="utf-8") as configFile:
        config.write(configFile)


def createFirmwareArchive(buildDir):
    archivePath = buildDir / customBuild.FIRMWARE_ARCHIVE
    with zipfile.ZipFile(archivePath, "w", compression=zipfile.ZIP_STORED) as archive:
        for name in customBuild.PUBLIC_BINARIES:
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            archive.writestr(info, (buildDir / name).read_bytes())


def writeDependencyInventory(buildDir, checkoutRoot, environment):
    version = runCommand(["pio", "--version"], checkoutRoot, capture=True, environment=environment).stdout
    packages = runCommand(
        ["pio", "pkg", "list", "-e", "esp32s3-custom"],
        checkoutRoot,
        capture=True,
        environment=environment,
    ).stdout
    (buildDir / "dependencies.txt").write_text(version + packages, encoding="utf-8")


def requireBuildFiles(buildDir):
    missing = [name for name in BUILD_FILES if not (buildDir / name).is_file()]
    if missing:
        raise ValueError(f"custom build did not produce: {missing}")


def completeCacheEntry(entryDir, expectedHash, expectedInput):
    if any(not (entryDir / name).is_file() for name in BUILD_FILES):
        return False
    try:
        manifest = json.loads((entryDir / "build-manifest.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    if manifest.get("combination_hash") != expectedHash:
        return False
    if manifest.get("combination_input") != expectedInput:
        return False
    binaries = manifest.get("binaries")
    if not isinstance(binaries, dict) or set(binaries) != set(customBuild.PUBLIC_BINARIES):
        return False
    try:
        with zipfile.ZipFile(entryDir / customBuild.FIRMWARE_ARCHIVE) as archive:
            if archive.namelist() != list(customBuild.PUBLIC_BINARIES):
                return False
            for name in customBuild.PUBLIC_BINARIES:
                payload = archive.read(name)
                if binaries.get(name) != {
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }:
                    return False
    except (OSError, KeyError, zipfile.BadZipFile):
        return False
    return (
        manifest.get("archive") == customBuild.fileMetadata(entryDir / customBuild.FIRMWARE_ARCHIVE)
        and manifest.get("dependencies") == customBuild.fileMetadata(entryDir / "dependencies.txt")
    )


def remoteCacheHit(baseUrl, expectedHash, expectedInput):
    parsed = urllib.parse.urlsplit(baseUrl)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError("remote cache URL must be HTTPS without credentials")
    if parsed.query or parsed.fragment:
        raise ValueError("remote cache URL cannot contain a query or fragment")
    manifestUrl = f"{baseUrl.rstrip('/')}/v1/{expectedHash}/build-manifest.json"
    request = urllib.request.Request(
        manifestUrl, headers={"User-Agent": "OpenScale-Custom-Build/1.0"}
    )
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            payload = response.read(MAX_REMOTE_MANIFEST_BYTES + 1)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return False
        raise ValueError(f"remote cache request failed: HTTP {error.code}") from error
    except urllib.error.URLError as error:
        raise ValueError(f"remote cache request failed: {error.reason}") from error
    if len(payload) > MAX_REMOTE_MANIFEST_BYTES:
        raise ValueError("remote cache manifest is too large")
    try:
        manifest = json.loads(payload)
    except (TypeError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("remote cache manifest is invalid") from error
    if manifest.get("combination_hash") != expectedHash:
        raise ValueError("remote cache manifest has the wrong combination hash")
    if manifest.get("combination_input") != expectedInput:
        raise ValueError("remote cache manifest has the wrong combination input")
    return True


def publishArtifacts(buildDir, outputDir):
    outputDir.parent.mkdir(parents=True, exist_ok=True)
    if outputDir.exists():
        raise ValueError(f"output directory already exists: {outputDir}")
    with tempfile.TemporaryDirectory(prefix="custom-output-", dir=outputDir.parent) as temporaryDirectory:
        ready = Path(temporaryDirectory) / outputDir.name
        ready.mkdir()
        for name in BUILD_FILES:
            shutil.copy2(buildDir / name, ready / name)
        ready.replace(outputDir)


def verifyPluginEnvironment(configPath, platformioEnvironment, sourceCommit=None):
    configuration = customBuild.resolveConfiguration(configPath)
    if not platformioEnvironment.startswith("esp32s3-"):
        raise ValueError("invalid plugin environment")
    pluginId = platformioEnvironment.removeprefix("esp32s3-")
    if configuration["requested_plugins"] != [pluginId]:
        raise ValueError("plugin verification requires one selected target plugin")
    pluginIds = [plugin["id"] for plugin in configuration["plugins"]]
    if pluginId not in pluginIds:
        raise ValueError("plugin environment does not match the selected plugin")
    if pluginId not in [patchPluginId for patchPluginId, _ in configuration["patches"]]:
        raise ValueError("plugin verification target has no matching patch")
    commitSha = (
        verifySourceCommit(ROOT, sourceCommit)
        if sourceCommit
        else resolveFirmwareCommit(ROOT, configuration["firmware_ref"])
    )
    with tempfile.TemporaryDirectory(prefix="openscale-plugin-") as temporaryDirectory:
        checkoutRoot = Path(temporaryDirectory) / "source"
        cloneSource(ROOT, commitSha, checkoutRoot)
        applyPatches(configuration, checkoutRoot)
        testPattern = f"test_{pluginId.replace('-', '_')}_*.py"
        tests = sorted((checkoutRoot / "tools").glob(testPattern))
        for testPath in tests:
            runCommand([sys.executable, str(testPath)], checkoutRoot)
        runCommand(["pio", "run", "-e", platformioEnvironment], checkoutRoot)
    return {
        "base_source": commitSha,
        "environment": platformioEnvironment,
        "plugin": pluginId,
        "tests": [path.name for path in tests],
    }


def buildCustomFirmware(
    configPath, outputRoot, sourceCommit=None, cacheUrl=None, expectedHash=None,
    builderCommit=None,
):
    configuration = customBuild.resolveConfiguration(configPath)
    if expectedHash is not None and not HASH_PATTERN.fullmatch(expectedHash):
        raise ValueError("expected combination hash must be 64 lowercase hex characters")
    builderSha = verifyBuilderCommit(ROOT, builderCommit)
    commitSha = (
        verifySourceCommit(ROOT, sourceCommit)
        if sourceCommit
        else resolveFirmwareCommit(ROOT, configuration["firmware_ref"])
    )
    with tempfile.TemporaryDirectory(prefix="openscale-custom-") as temporaryDirectory:
        checkoutRoot = Path(temporaryDirectory) / "source"
        cloneSource(ROOT, commitSha, checkoutRoot)
        identity = customBuild.combinationInput(
            configuration, commitSha, checkoutRoot, builderSha
        )
        combinationHash = customBuild.combinationHash(identity)
        if expectedHash is not None and combinationHash != expectedHash:
            raise ValueError("build selection does not match the expected combination hash")
        outputDir = outputRoot / combinationHash
        if completeCacheEntry(outputDir, combinationHash, identity):
            return {
                "base_source": commitSha,
                "builder_source": builderSha,
                "cache_hit": True,
                "combination_hash": combinationHash,
                "output": str(outputDir),
            }
        if outputDir.exists():
            raise ValueError(f"incomplete cache entry already exists: {outputDir}")
        if cacheUrl and remoteCacheHit(cacheUrl, combinationHash, identity):
            return {
                "base_source": commitSha,
                "builder_source": builderSha,
                "cache_hit": True,
                "combination_hash": combinationHash,
                "output": str(outputDir),
            }
        applyPatches(configuration, checkoutRoot)
        prepareBuildCheckout(checkoutRoot)
        sourceEpoch = sourceDateEpoch(checkoutRoot)
        environment = {
            **os.environ,
            "HDS_CUSTOM_BUILD_CATALOG_ROOT": str(ROOT),
            "HDS_CUSTOM_BUILD_CONFIG": str(configPath),
            "HDS_FIRMWARE_VERSION": identity["firmware_version"],
            "SOURCE_DATE_EPOCH": sourceEpoch,
        }
        runCommand(["pio", "run", "-e", "esp32s3-custom"], checkoutRoot, environment=environment)
        runCommand(
            ["pio", "run", "-e", "esp32s3-custom", "-t", "buildfs"],
            checkoutRoot,
            environment=environment,
        )
        buildDir = checkoutRoot / ".pio.nosync" / "build" / "esp32s3-custom"
        writeDependencyInventory(buildDir, checkoutRoot, environment)
        createFirmwareArchive(buildDir)
        customBuild.writeBuildManifest(
            configuration,
            buildDir,
            buildDir / "build-manifest.json",
            commitSha,
            checkoutRoot,
            identity,
        )
        requireBuildFiles(buildDir)
        publishArtifacts(buildDir, outputDir)
    return {
        "base_source": commitSha,
        "builder_source": builderSha,
        "cache_hit": False,
        "combination_hash": combinationHash,
        "output": str(outputDir),
    }


def writeGithubOutput(path, result):
    with path.open("a", encoding="utf-8") as output:
        for key in ("combination_hash", "output", "cache_hit"):
            output.write(f"{key}={str(result[key]).lower() if key == 'cache_hit' else result[key]}\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=ROOT / "custom-build.json")
    parser.add_argument("--output", type=Path, default=ROOT / ".pio.nosync" / "custom-output")
    parser.add_argument("--github-output", type=Path)
    parser.add_argument("--cache-url")
    parser.add_argument("--builder-commit")
    parser.add_argument("--source-commit")
    parser.add_argument("--expected-hash")
    parser.add_argument("--verify-plugin-environment")
    args = parser.parse_args()
    sourceCommit = requestedSourceCommit(args.source_commit, args.verify_plugin_environment)
    if args.verify_plugin_environment and any((args.github_output, args.cache_url, args.expected_hash)):
        parser.error("plugin verification does not publish or use the artifact cache")
    try:
        if args.verify_plugin_environment:
            result = verifyPluginEnvironment(
                args.config.resolve(), args.verify_plugin_environment, sourceCommit
            )
        else:
            result = buildCustomFirmware(
                args.config.resolve(),
                args.output.resolve(),
                sourceCommit,
                args.cache_url,
                args.expected_hash or None,
                args.builder_commit or None,
            )
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"custom build failed: {error}\n")
    if args.github_output:
        writeGithubOutput(args.github_output.resolve(), result)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
