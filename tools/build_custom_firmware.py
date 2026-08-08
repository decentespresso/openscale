import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import configure_custom_build as customBuild


ROOT = Path(__file__).resolve().parents[1]
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
BUILD_FILES = (
    "firmware.bin",
    "bootloader.bin",
    "partitions.bin",
    "littlefs.bin",
    "build-manifest.json",
    "dependencies.txt",
)


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


def pullRequestCommit():
    if os.environ.get("GITHUB_EVENT_NAME") != "pull_request":
        return None
    sourceCommit = os.environ.get("GITHUB_SHA")
    if not sourceCommit:
        raise ValueError("pull request build has no GITHUB_SHA")
    return sourceCommit


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


def buildCustomFirmware(configPath, outputDir, sourceCommit=None):
    configuration = customBuild.resolveConfiguration(configPath)
    if outputDir.exists():
        raise ValueError(f"output directory already exists: {outputDir}")
    commitSha = (
        verifySourceCommit(ROOT, sourceCommit)
        if sourceCommit
        else resolveFirmwareCommit(ROOT, configuration["firmware_ref"])
    )
    with tempfile.TemporaryDirectory(prefix="openscale-custom-") as temporaryDirectory:
        checkoutRoot = Path(temporaryDirectory) / "source"
        cloneSource(ROOT, commitSha, checkoutRoot)
        applyPatches(configuration, checkoutRoot)
        environment = {
            **os.environ,
            "HDS_CUSTOM_BUILD_CATALOG_ROOT": str(ROOT),
            "HDS_CUSTOM_BUILD_CONFIG": str(configPath),
        }
        runCommand(["pio", "run", "-e", "esp32s3-custom"], checkoutRoot, environment=environment)
        runCommand(
            ["pio", "run", "-e", "esp32s3-custom", "-t", "buildfs"],
            checkoutRoot,
            environment=environment,
        )
        buildDir = checkoutRoot / ".pio.nosync" / "build" / "esp32s3-custom"
        writeDependencyInventory(buildDir, checkoutRoot, environment)
        customBuild.writeBuildManifest(
            configuration,
            buildDir,
            buildDir / "build-manifest.json",
            commitSha,
            checkoutRoot,
        )
        requireBuildFiles(buildDir)
        publishArtifacts(buildDir, outputDir)
    return commitSha


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=ROOT / "custom-build.json")
    parser.add_argument("--output", type=Path, default=ROOT / ".pio.nosync" / "custom-output")
    args = parser.parse_args()
    try:
        commitSha = buildCustomFirmware(args.config.resolve(), args.output.resolve(), pullRequestCommit())
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"custom build failed: {error}\n")
    print(json.dumps({"base_source": commitSha, "output": str(args.output.resolve())}, sort_keys=True))


if __name__ == "__main__":
    main()
