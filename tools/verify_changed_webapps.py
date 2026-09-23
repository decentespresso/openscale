import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

import configure_custom_build as customBuild


ROOT = Path(__file__).resolve().parents[1]
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")


def changedPluginIds(baseSha, headSha):
    if not SHA_PATTERN.fullmatch(baseSha) or not SHA_PATTERN.fullmatch(headSha):
        raise ValueError("base and head must be full commit SHAs")
    output = subprocess.run(
        ["git", "diff", "--name-only", f"{baseSha}...{headSha}", "--", "plugins/"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    ).stdout
    return sorted({
        parts[1]
        for line in output.splitlines()
        if len(parts := line.replace("\\", "/").split("/")) >= 3
        and parts[0] == "plugins"
    })


def verify(baseSha, headSha):
    catalog = customBuild.loadPluginCatalog()
    pluginIds = [
        pluginId for pluginId in changedPluginIds(baseSha, headSha)
        if pluginId in catalog and "webapp" in catalog[pluginId][0]
    ]
    for pluginId in pluginIds:
        with tempfile.TemporaryDirectory(prefix="webapp-build-") as temporaryDirectory:
            configPath = Path(temporaryDirectory) / "selection.json"
            configPath.write_text(json.dumps({
                "firmware_ref": "main", "features": [], "plugins": [pluginId],
            }), encoding="utf-8")
            customBuild.resolveConfiguration(configPath)
            environment = {**os.environ, "HDS_CUSTOM_BUILD_CONFIG": str(configPath)}
            subprocess.run(
                ["pio", "run", "-e", "esp32s3-custom", "-t", "buildfs"],
                cwd=ROOT, env=environment, check=True,
            )
    print(f"verified {len(pluginIds)} changed webapp filesystem selections")


if __name__ == "__main__":
    verify(os.environ["BASE_SHA"], os.environ["HEAD_SHA"])
