import argparse
import json
from pathlib import Path
import re
import subprocess

import configure_custom_build as customBuild


ROOT = Path(__file__).resolve().parents[1]
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")


def changedPaths(base, head, repositoryRoot=ROOT):
    if not SHA_PATTERN.fullmatch(base) or not SHA_PATTERN.fullmatch(head):
        raise ValueError("base and head must be full lowercase SHA-1 values")
    result = subprocess.run(
        [
            "git",
            "diff",
            "--name-only",
            "--diff-filter=ACMRTD",
            f"{base}...{head}",
            "--",
            "plugins",
        ],
        cwd=repositoryRoot,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.splitlines()


def changedPluginIds(paths):
    pluginIds = set()
    for path in paths:
        parts = path.replace("\\", "/").split("/")
        if len(parts) >= 3 and parts[0] == "plugins" and customBuild.ID_PATTERN.fullmatch(parts[1]):
            pluginIds.add(parts[1])
    return sorted(pluginIds)


def affectedPluginIds(pluginCatalog, changedIds):
    affected = set(changedIds) & set(pluginCatalog)
    while True:
        expanded = affected | {
            pluginId
            for pluginId, (manifest, _, _) in pluginCatalog.items()
            if set(manifest["depends_on"]) & affected
        }
        if expanded == affected:
            return sorted(affected)
        affected = expanded


def changedPluginMatrix(paths, pluginCatalog=None):
    if pluginCatalog is None:
        pluginCatalog = customBuild.loadPluginCatalog()
    matrix = []
    for pluginId in affectedPluginIds(pluginCatalog, changedPluginIds(paths)):
        _, _, patches = pluginCatalog[pluginId]
        matrix.extend(
            {"plugin": pluginId, "firmware_ref": firmwareRef}
            for firmwareRef in sorted(patches)
        )
    return matrix


def writeGithubOutput(path, matrix):
    serialized = json.dumps(matrix, separators=(",", ":"))
    with path.open("a", encoding="utf-8") as output:
        output.write(f"count={len(matrix)}\n")
        output.write(f"matrix={serialized}\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args()
    try:
        matrix = changedPluginMatrix(changedPaths(args.base, args.head))
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"changed plugin detection failed: {error}\n")
    if args.github_output:
        writeGithubOutput(args.github_output, matrix)
    print(json.dumps(matrix, sort_keys=True))


if __name__ == "__main__":
    main()
