import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(text, contents):
    assert text in contents, f"missing contract: {text}"


def require_guarded(contents, text):
    matches = list(re.finditer(re.escape(text), contents))
    assert matches, f"missing guarded contract: {text}"
    for match in matches:
        start = contents.rfind("#if HDS_ENABLE_GRINDER", 0, match.start())
        end = contents.find("#endif", match.end())
        assert start >= 0 and end >= 0, f"unguarded grinder contract: {text}"


def main():
    config = source("include/config.h")
    platformio = source("platformio.ini")
    hds = source("src/hds.ino")
    menu = source("include/menu.h")
    parameter = source("include/parameter.h")
    power = source("include/power.h")
    wifi = source("src/wifi_setup.cpp")
    wifi_header = source("include/wifi_setup.h")

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

    require("bool wifiEnsureMdnsReadyForSta()", wifi)
    require('MDNS.addService("decentscale", "tcp", 80)', wifi)
    require("g_mdnsAdvertisePending", wifi)
    require("bool wifiEnsureMdnsReadyForSta();", wifi_header)
    assert "HDS_ENABLE_GRINDER" not in wifi
    assert "#ifdef HDS_ENABLE_GRINDER" not in "".join((hds, menu, parameter, power))

    print("grinder feature flag contract tests passed")


if __name__ == "__main__":
    main()
