import re
import unittest
from pathlib import Path
from test_grinder_feature_flag_contract import is_guarded_at


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


POLICY = read("include/energy_policy.h")
RUNTIME = read("include/energy_runtime_policy.h")
MENU = read("include/energy_menu.h")
MENU_INTEGRATION = read("include/menu.h")
STORAGE = read("include/storage.h")
PARAMETER = read("include/parameter.h")
FIRMWARE = read("src/hds.ino")
POWER = read("include/power.h")
GYRO = read("include/gyro.h")
WIFI = read("src/wifi_setup.cpp")
FEATURES = read("include/hds_features.h")
PLATFORMIO = read("platformio.ini")
CUSTOM_BUILD = read("tools/configure_custom_build.py")
NIGHTLY = read(".github/workflows/nightly.yml")
ENERGY_SOURCES = POLICY + RUNTIME + MENU + STORAGE + PARAMETER + FIRMWARE + POWER + GYRO + WIFI
ACC_GUARD = "defined(ACC_PWR_CTRL) && defined(V8_1) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)"


def body(source, signature):
    start = source.rindex(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"body not closed: {signature}")


class EnergyContractTests(unittest.TestCase):
    def assert_guarded(self, source, text):
        matches = list(re.finditer(re.escape(text), source))
        self.assertTrue(matches, f"missing guarded contract: {text}")
        self.assertTrue(
            all(is_guarded_at(source, match.start(), "HDS_ENABLE_ENERGY_MENU") for match in matches),
            f"unguarded energy contract: {text}",
        )

    def test_compile_time_gate_matches_grinder_pattern(self):
        self.assertIn("#define HDS_FEATURE_ENERGY_MENU 0", FEATURES)
        self.assertIn("#define HDS_ENABLE_ENERGY_MENU HDS_FEATURE_ENERGY_MENU", FEATURES)
        normal = PLATFORMIO.split("[env:esp32s3]", 1)[1].split("[env:esp32s3-grinder]", 1)[0]
        energy_environment = PLATFORMIO.split("[env:esp32s3-energy-menu]", 1)[1].split("[env:esp32s3-custom]", 1)[0]
        self.assertNotIn("HDS_ENABLE_ENERGY_MENU", normal)
        self.assertIn("extends = env:esp32s3", energy_environment)
        self.assertIn("${env:esp32s3.build_flags}", energy_environment)
        self.assertIn("-DHDS_ENABLE_ENERGY_MENU=1", energy_environment)
        self.assertIn('"energy-menu": ("HDS_FEATURE_ENERGY_MENU", ())', CUSTOM_BUILD)
        self.assertIn("python tools/test_energy_stage0_contract.py", NIGHTLY)
        self.assertNotIn("- esp32s3-energy-menu", NIGHTLY)
        for source, text in [
            (MENU_INTEGRATION, '#include "energy_menu.h"'),
            (MENU_INTEGRATION, "&menuEnergy"),
            (PARAMETER, "EnergyPolicy energyPolicy"),
            (STORAGE, "KEY_ENERGY_SCHEMA"),
            (FIRMWARE, "energyLoadSettings(energyPolicy.settings)"),
            (FIRMWARE, "serviceEnergyHousekeeping(millis())"),
            (POWER, "processNewBatterySample()"),
            (GYRO, "featureEnabled(EnergyFeature::MotionPoll)"),
            (WIFI, "featureEnabled(EnergyFeature::SerialQuiet)"),
        ]:
            self.assert_guarded(source, text)

    def test_menu_rows_and_order(self):
        labels = re.findall(r'char menuEnergy\w+Label\[\] = "([^"]+)";', MENU)
        self.assertEqual(
            [
                "Serial Quiet o", "Power Cadence o", "OLED Redraw o",
                "OLED Idle o", "OLED Static o", "Motion Poll o", "ACC Rail Off o",
            ],
            labels,
        )
        self.assertTrue(all(label.endswith(" o") for label in labels))
        entries = body(MENU, "Menu *energyMenu[]")
        names = re.findall(r"&menuEnergy(\w+)", entries)
        self.assertEqual(
            ["Back", "SerialQuiet", "PowerCadence", "OledRedraw", "OledIdle", "OledStatic", "MotionPoll", "AccRailOff"],
            names,
        )

    def test_toggle_updates_only_selected_row(self):
        update = body(MENU, "inline void updateEnergyMenuRow")
        self.assertIn("energyFeatureRows[index]", update)
        self.assertIn("? 'x' : 'o'", update)
        toggle = body(MENU, "inline void toggleEnergyFeature")
        self.assertEqual(1, toggle.count("updateEnergyMenuRow(feature)"))
        self.assertIn("energyStoreFeature(feature, enabled)", toggle)
        self.assertIn("applyEnergyFeatureTransition(feature, wasEnabled, enabled)", toggle)
        action = body(MENU, "inline void showEnergyAction")
        self.assertIn('enabled ? "ON" : "OFF"', action)

    def test_persisted_rows_are_refreshed_at_boot(self):
        setup = body(FIRMWARE, "void setup()")
        self.assertLess(setup.index("energyLoadSettings(energyPolicy.settings)"),
                        setup.index("refreshEnergyMenuRows()"))
        self.assertIn("ENERGY_SCHEMA_VERSION = 4", STORAGE)
        self.assertIn('"e_acc_rail"', STORAGE)
        self.assertIn("storageLoadValidatedBool", body(STORAGE, "inline bool energyLoadSettings"))

    def test_status_and_statistics_are_removed(self):
        for removed in [
            "Energy Status", "Reset Stats", "EnergyStats", "EnergyState",
            "EnergyActivity", "showEnergyStatus",
            "resetEnergyStats", "printEnergyStatus", "drawEnergyStatusPage",
        ]:
            self.assertNotRegex(ENERGY_SOURCES, rf"\b{re.escape(removed)}\b")
        self.assertNotIn("energyPolicy.stats", ENERGY_SOURCES)

    def test_acc_rail_is_guarded_and_applied_immediately(self):
        for source in [POLICY, MENU, STORAGE, FIRMWARE]:
            self.assertIn(ACC_GUARD, source)
        transition = body(FIRMWARE, "void applyEnergyFeatureTransition")
        self.assertIn("feature == EnergyFeature::AccRailOff", transition)
        self.assertIn("applyEnergyAccRailState()", transition)
        apply_state = body(FIRMWARE, "void applyEnergyAccRailState")
        self.assertIn("EnergyFeature::AccRailOff", apply_state)
        self.assertIn("? LOW : HIGH", apply_state)

    def test_acc_rail_is_not_polled_and_deep_sleep_remains_low(self):
        loop = body(FIRMWARE, "void loop()")
        self.assertNotIn("EnergyFeature::AccRailOff", loop)
        self.assertNotIn("applyEnergyAccRailState", loop)
        self.assertIn("digitalWrite(ACC_PWR_CTRL, LOW);", POWER)
        self.assertIn("gpio_hold_en((gpio_num_t)ACC_PWR_CTRL);", POWER)

    def test_accelerometer_builds_exclude_acc_rail_toggle(self):
        guarded_menu = re.findall(rf"#if {re.escape(ACC_GUARD)}(.*?)#endif", MENU, re.S)
        self.assertTrue(guarded_menu)
        self.assertTrue(all("AccRailOff" in section for section in guarded_menu))

    def test_retained_features_keep_their_natural_call_sites(self):
        self.assertIn("featureEnabled(EnergyFeature::SerialQuiet)", POWER + WIFI + FIRMWARE)
        self.assertIn("featureEnabled(EnergyFeature::PowerCadence)", POWER)
        self.assertIn("featureEnabled(EnergyFeature::MotionPoll)", GYRO)
        update_oled = body(FIRMWARE, "void updateOled()")
        self.assertIn("featureEnabled(EnergyFeature::OledRedraw)", update_oled)
        self.assertIn("featureEnabled(EnergyFeature::OledStatic)", update_oled)
        self.assertIn("energyRuntime.explicitDisplayOff", FIRMWARE)

    def test_soft_sleep_suspends_oled_idle_and_wake_resynchronizes(self):
        idle = body(FIRMWARE, "void applyEnergyDisplayIdle")
        self.assertIn("shouldApplyDisplayIdle(b_softSleep)", idle)

        housekeeping = body(FIRMWARE, "void serviceEnergyHousekeeping")
        self.assertLess(housekeeping.index("enabledMask == 0"),
                        housekeeping.index("b_softSleep"))
        self.assertLess(housekeeping.index("b_softSleep"),
                        housekeeping.index("applyEnergyDisplayIdle(now)"))

        wake = body(FIRMWARE, "bool wakeScaleFromSoftSleep")
        wake_steps = [
            "b_softSleep = false;",
            "clearPendingEnergyActivity();",
            "energyPolicy.recordActivity(millis());",
            "applyEnergyDisplayCommand(!energyRuntime.explicitDisplayOff);",
        ]
        self.assertEqual(wake_steps, sorted(wake_steps, key=wake.index))

    def test_disabled_power_cadence_keeps_legacy_low_battery_path(self):
        legacy = body(POWER, "bool processLegacyLowBattery")
        self.assertIn("lowBatteryConfirmed(i_lowBatteryCount, false)", legacy)
        self.assertIn("shut_down_low_battery(f_batteryVoltage)", legacy)

        cadence = body(POWER, "bool processNewBatterySample")
        self.assertIn("lowBatteryConfirmed(i_lowBatteryCount, true)", cadence)

        for signature in ["void power_off(int min)", "void power_off(double sec)"]:
            power_off = body(POWER, signature)
            self.assertIn("featureEnabled(EnergyFeature::PowerCadence)", power_off)
            self.assertIn("cadenceEnabled && processNewBatterySample()", power_off)
            self.assertIn("!cadenceEnabled && !b_is_charging && processLegacyLowBattery()", power_off)

    def test_no_master_or_extra_energy_menu_was_added(self):
        self.assertNotRegex(MENU, r"Experiments|Energy Fast|NVS|Storage|Persistence")


if __name__ == "__main__":
    unittest.main()
