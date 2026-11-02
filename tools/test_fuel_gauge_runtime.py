import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_auto_off_weight_activity import extract_function


ROOT = Path(__file__).resolve().parents[1]


def checkStartupOrder(compiler):
    header = (ROOT / "include/boot_startup.h").read_text(encoding="utf-8")
    setup = extract_function((ROOT / "src/hds.ino").read_text(encoding="utf-8"), "void setup()")
    acceptance = setup.index("fuelGaugeAllowConfiguration();")
    assert setup.index("hdsOtaRollbackMarkValid();") < acceptance
    assert setup.index("pullOtaResumePendingLittleFs()") < acceptance
    assert setup.count("fuelGaugeAllowConfiguration();") == 1
    gate = extract_function(setup, "if (!b_ota && !bootOtaRebootPending)")
    assert "fuelGaugeAllowConfiguration();" in gate
    debtLoaded = setup.index("fuelGaugeSetResealPending(")
    discovery = setup.index("fuelGaugeStartDetection(")
    deferredRollback = setup.index("if (bootRollbackReason) hdsOtaRollback(bootRollbackReason);")
    assert debtLoaded < discovery < deferredRollback < setup.index("if (fuelGaugeLegacyHardware())")
    assert setup.index("bootRollbackReason = hdsOtaRollbackBegin(r);") < setup.index("storageInit(&bootSettingsOpened)")
    earlySleep = setup.index("esp32_sleep();")
    assert earlySleep < setup.index("Wire.begin(I2C_SDA, I2C_SCL);") < debtLoaded
    for operation in ("Wire.begin(", "fuelGaugeStartDetection(", "fuelGaugeBegin(", "serviceFuelGauge("):
        assert operation not in setup[:earlySleep]
    profiles = (
        (("V8_1", "ADS1232ADC", "ADS1115ADC", "HW_SPI"), True),
        (("V8_1", "ADS1232ADC", "ADS1115ADC", "HW_SPI", "ACC_BMA400"), False),
        (("V8_1", "ADS1232ADC", "ADS1115ADC", "HW_SPI", "ACC_MPU6050"), False),
        (("V8_0", "ADS1232ADC", "ADS1115ADC", "HW_SPI", "ACC_BMA400"), False),
        (("V7_4", "ADS1232ADC", "HW_SPI"), False),
    )
    for defines, overlap in profiles:
        result = subprocess.run(
            [compiler, "-E", "-P", "-x", "c++", "-", "-DESP32", *[f"-D{name}" for name in defines]],
            input=header + "\n" + setup, text=True, capture_output=True, check=True,
        )
        body = extract_function(result.stdout, "void setup()")
        if overlap:
            snippets = (
                "pinMode(ACC_PWR_CTRL, INPUT);", "gpio_hold_dis((gpio_num_t)ACC_PWR_CTRL);",
                "SPI.begin(", "delay(50);", "initializeBootDisplay();", "renderBootLogo();",
                "fuelGaugeStartDetection(", "scale.begin();", "scale.setSamplesInUse(1);",
                "stabilizeScaleWithFuelGauge();", "if (fuelGaugeLegacyHardware())",
                "digitalWrite(ACC_PWR_CTRL, HIGH);", "ADS_init();", "power_off(15);",
            )
            assert "fuelGaugeBegin(" not in body and "scale.start(" not in body
            assert "fuelGaugeDrain(" not in body
        else:
            assert body.index("fuelGaugeDrain();") < body.rindex("t_bootTare = millis();")
            snippets = (
                "fuelGaugeBegin(", "digitalWrite(ACC_PWR_CTRL, HIGH);", "SPI.begin(",
                "delay(50);", "initializeBootDisplay();", "power_off(15);",
                "renderBootLogo();", "scale.begin();", "scale.start(stabilizingtime, false);",
            )
            assert "fuelGaugeStartDetection(" not in body and "stabilizeScaleWithFuelGauge();" not in body
            if "ACC_BMA400" in defines or "ACC_MPU6050" in defines:
                assert body.index("ACC_init();") < body.index("initializeBootDisplay();")
        cursor = 0
        for snippet in snippets:
            position = body.find(snippet, cursor)
            assert position >= 0, f"{defines}: missing ordered startup step {snippet}"
            cursor = position + len(snippet)
        assert body.count("delay(50);") == 1
        assert body.count("renderBootLogo();") == 1


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    with tempfile.TemporaryDirectory(prefix="fuel-gauge-runtime-") as directory:
        for name in ("runtime", "startup"):
            binary = Path(directory) / (f"{name}.exe" if os.name == "nt" else name)
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DESP32",
                "-I", str(ROOT / "tools/fuel_gauge_test"), "-I", str(ROOT / "include"),
                str(ROOT / "src/fuel_gauge.cpp"), str(ROOT / "src/fuel_gauge_config.cpp"),
                str(ROOT / "src/fuel_gauge_engine.cpp"),
                str(ROOT / f"tools/fuel_gauge_test/{name}.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
    checkStartupOrder(compiler)
    print("fuel gauge runtime and startup tests passed")


if __name__ == "__main__":
    main()
