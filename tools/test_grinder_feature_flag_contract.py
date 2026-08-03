import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(text, contents):
    assert text in contents, f"missing contract: {text}"


DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def is_grinder_condition(kind, expression):
    normalized = re.sub(r"\s+", "", expression)
    if kind == "ifdef":
        return normalized == "HDS_ENABLE_GRINDER"
    if kind in ("if", "elif"):
        return normalized in ("HDS_ENABLE_GRINDER", "defined(HDS_ENABLE_GRINDER)")
    return False


def is_guarded_at(contents, offset):
    stack = []
    position = 0
    for line in contents.splitlines(keepends=True):
        if position >= offset:
            break
        directive = DIRECTIVE.match(line)
        if directive:
            kind, expression = directive.groups()
            if kind in ("if", "ifdef", "ifndef"):
                stack.append(is_grinder_condition(kind, expression))
            elif kind == "elif":
                assert stack, "unbalanced preprocessor elif"
                stack[-1] = is_grinder_condition(kind, expression)
            elif kind == "else":
                assert stack, "unbalanced preprocessor else"
                stack[-1] = False
            elif kind == "endif":
                assert stack, "unbalanced preprocessor endif"
                stack.pop()
        position += len(line)
    return any(stack)


def require_guarded(contents, text):
    matches = list(re.finditer(re.escape(text), contents))
    assert matches, f"missing guarded contract: {text}"
    for match in matches:
        assert is_guarded_at(contents, match.start()), f"unguarded grinder contract: {text}"


def require_guarded_rejects_sibling():
    contents = """#if HDS_ENABLE_GRINDER
value();
#endif
value();
#if OTHER_FEATURE
other();
#endif
"""
    try:
        require_guarded(contents, "value();")
    except AssertionError:
        return
    raise AssertionError("guard contract accepted a sibling occurrence")


def main():
    config = source("include/config.h")
    platformio = source("platformio.ini")
    hds = source("src/hds.ino")
    menu = source("include/menu.h")
    parameter = source("include/parameter.h")
    power = source("include/power.h")
    wifi = source("src/wifi_setup.cpp")
    wifi_header = source("include/wifi_setup.h")
    nightly = source(".github/workflows/nightly.yml")

    require_guarded_rejects_sibling()

    require("#ifndef HDS_ENABLE_GRINDER\n#define HDS_ENABLE_GRINDER 0\n#endif", config)
    normal = platformio.split("[env:esp32s3]", 1)[1].split("[env:esp32s3-grinder]", 1)[0]
    grinder_environment = platformio.split("[env:esp32s3-grinder]", 1)[1].split("[env:native]", 1)[0]
    assert "HDS_ENABLE_GRINDER" not in normal
    require("extends = env:esp32s3", grinder_environment)
    require("${env:esp32s3.build_flags}", grinder_environment)
    require("-DHDS_ENABLE_GRINDER=1", grinder_environment)

    require_guarded(hds, '#include "grinder_runtime.h"')
    for text in (
        "grinderLoadSettings();",
        "grinderRuntimeBegin();",
        "grinderRuntimeTick(f_displayedValue);",
        "drawGrinder();",
    ):
        require_guarded(hds, text)
    require_guarded(menu, '#include "grinder_runtime.h"')
    for text in ("menuGrinder", '"Grinder Plug"', "grinderSetActionMessage"):
        require_guarded(menu, text)
    for text in (
        "GrinderSettings grinderSettings",
        "GrinderRuntime grinderRuntime",
        "grinderMdnsCandidateBuffer",
        "f_grinder_fast_weight",
        "grinderFastWeightSequence",
    ):
        require_guarded(parameter, text)
    require_guarded(power, "beforeDeepSleepFlush")
    require("python tools/test_grinder_feature_flag_contract.py", nightly)
    require("- esp32s3-grinder", nightly)
    artifact_guard = "if: matrix.board == 'esp32s3'\n        uses: actions/upload-artifact@v4"
    assert nightly.count(artifact_guard) == 2, "grinder artifact upload is not gated"

    require("bool wifiEnsureMdnsReadyForSta()", wifi)
    require('MDNS.addService("decentscale", "tcp", 80)', wifi)
    require("g_mdnsAdvertisePending", wifi)
    require("bool wifiEnsureMdnsReadyForSta();", wifi_header)
    assert "HDS_ENABLE_GRINDER" not in wifi
    assert "#ifdef HDS_ENABLE_GRINDER" not in "".join((hds, menu, parameter, power))

    print("grinder feature flag contract tests passed")


if __name__ == "__main__":
    main()
