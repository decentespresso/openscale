"""Pre-build advisory: warn when the pinned pioarduino platform is behind stable.

We pin `platform =` in platformio.ini to an explicit pioarduino version (not the
rolling `stable` tag) so builds are reproducible. The trade-off is that we have
to remember to bump it to pick up framework fixes. This hook compares the pinned
version against the latest pioarduino release and prints a warning -- plus a
GitHub Actions annotation in CI -- when a newer one exists.

It is advisory only: it never fails the build, and it stays silent when offline,
rate-limited, or when the platform isn't pinned to an explicit version. Set
HDS_SKIP_PLATFORM_CHECK=1 to skip it entirely (e.g. fast offline builds).
"""
import json
import os
import re
import urllib.request

# PlatformIO exec's extra_scripts without a module __file__; it runs from the
# project root, so resolve platformio.ini relative to the working directory.
PLATFORMIO_INI = os.path.join(os.getcwd(), "platformio.ini")
LATEST_API = "https://api.github.com/repos/pioarduino/platform-espressif32/releases/latest"
# Matches an explicitly versioned pioarduino platform pin; the rolling `stable`
# tag deliberately does NOT match, so this stays quiet until we pin a version.
PIN_RE = re.compile(
    r"pioarduino/platform-espressif32/releases/download/"
    r"([0-9]+\.[0-9]+\.[0-9]+[^/]*)/platform-espressif32\.zip"
)


def _warn(msg):
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::warning title=Platform behind stable::" + msg)
    print("\033[33m[platform-check] " + msg + "\033[0m")


def _version_tuple(v):
    return tuple(int(x) for x in re.findall(r"\d+", v))


def main():
    if os.environ.get("HDS_SKIP_PLATFORM_CHECK"):
        return
    try:
        with open(PLATFORMIO_INI, "r") as f:
            ini = f.read()
    except OSError:
        return

    match = PIN_RE.search(ini)
    if not match:
        return  # not pinned to an explicit version -> nothing to compare
    pinned = match.group(1)

    try:
        req = urllib.request.Request(
            LATEST_API, headers={"User-Agent": "hds-platform-check"}
        )
        token = os.environ.get("GITHUB_TOKEN")
        if token:
            req.add_header("Authorization", "Bearer " + token)
        with urllib.request.urlopen(req, timeout=4) as resp:
            latest = json.load(resp).get("tag_name", "")
    except Exception:
        return  # offline / rate-limited / API change -> never block the build

    if latest and _version_tuple(latest) > _version_tuple(pinned):
        _warn(
            "pinned pioarduino platform {p} is behind latest stable {l}. "
            "Consider bumping `platform =` in platformio.ini "
            "(https://github.com/pioarduino/platform-espressif32/releases/tag/{l}).".format(
                p=pinned, l=latest
            )
        )


try:
    main()
except Exception:
    # Advisory only -- never let a bug in this check fail the build.
    pass
