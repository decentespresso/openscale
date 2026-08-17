import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POLICY = (ROOT / "include" / "energy_policy.h").read_text(encoding="utf-8")
MENU = (ROOT / "include" / "energy_menu.h").read_text(encoding="utf-8")
STORAGE = (ROOT / "include" / "storage.h").read_text(encoding="utf-8")
POWER = (ROOT / "include" / "energy_power_management.h").read_text(encoding="utf-8")
SHUTDOWN = (ROOT / "include" / "power.h").read_text(encoding="utf-8")
FIRMWARE = (ROOT / "src" / "hds.ino").read_text(encoding="utf-8")
HDS_FEATURES = (ROOT / "include" / "hds_features.h").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")
SDKCONFIG = (ROOT / "sdkconfig.energy-menu.defaults").read_text(encoding="utf-8")
DOCS = (ROOT / "docs" / "energy-saving-stage-0.md").read_text(encoding="utf-8")


def body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(signature)


class EnergyLightSleepContractTests(unittest.TestCase):
    def test_feature_count_and_menu_order(self):
        self.assertEqual(
            ["SerialQuiet", "PowerCadence", "OledRedraw", "OledIdle", "OledStatic", "LightSleep"],
            re.findall(r"^  (\w+),$", POLICY, re.MULTILINE),
        )
        self.assertEqual(
            [
                "Serial Quiet o", "Power Cadence o", "OLED Redraw o",
                "OLED Idle o", "OLED Static o", "Light Sleep o",
            ],
            re.findall(r'char menuEnergy\w+Label\[\] = "([^"]+)";', MENU),
        )
        self.assertNotIn("Motion Poll", MENU)
        self.assertNotIn("ACC Rail Off", MENU)
        self.assertIn("static_cast<size_t>(EnergyFeature::Count)", MENU)

    def test_storage_migration_preserves_retained_features(self):
        self.assertIn("ENERGY_SCHEMA_VERSION = 5", STORAGE)
        self.assertIn('"e_light_sleep"', STORAGE)
        self.assertIn("KEY_ENERGY_MOTION_POLL", STORAGE)
        self.assertIn("KEY_ENERGY_ACC_RAIL_OFF", STORAGE)
        migration = body(STORAGE, "inline bool energyLoadSettings")
        self.assertIn("for (uint8_t index = 0; index < 5; ++index)", migration)
        self.assertIn("storagePutBool(ENERGY_FEATURE_KEYS[5], false)", migration)
        self.assertIn("storageRemoveIfPresent(KEY_ENERGY_MOTION_POLL)", migration)
        self.assertIn("storageRemoveIfPresent(KEY_ENERGY_ACC_RAIL_OFF)", migration)
        self.assertIn("getType(key) == PT_U8", STORAGE)

    def test_runtime_transition_is_centralized(self):
        transition = body(FIRMWARE, "void applyEnergyFeatureTransition(EnergyFeature feature, bool wasEnabled, bool isEnabled) {")
        self.assertIn("feature == EnergyFeature::LightSleep", transition)
        self.assertIn("applyEnergyLightSleepSetting(isEnabled)", transition)
        self.assertIn("initEnergyPowerManagement()", FIRMWARE)
        self.assertIn("serviceEnergyPowerManagement()", FIRMWARE)
        self.assertIn("setEnergyPerformanceCritical(b_ota || b_pullOtaRunning)", FIRMWARE)

    def test_pm_lock_policy(self):
        self.assertIn("ESP_PM_CPU_FREQ_MAX", POWER)
        self.assertIn("ESP_PM_NO_LIGHT_SLEEP", POWER)
        self.assertIn("config.max_freq_mhz = 240", POWER)
        self.assertIn("config.min_freq_mhz = 80", POWER)
        self.assertIn("config.light_sleep_enable = true", POWER)
        self.assertEqual(POWER.count("esp_pm_lock_create("), 4)
        self.assertEqual(POWER.count("esp_pm_lock_delete("), 1)
        deep_sleep = body(SHUTDOWN, "void esp32_sleep()")
        self.assertLess(
            deep_sleep.index("applyEnergyLightSleepSetting(false)"),
            deep_sleep.index("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)"),
        )
        self.assertLess(
            deep_sleep.index("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)"),
            deep_sleep.index("esp_sleep_enable_ext1_wakeup_io"),
        )

    def test_no_loop_sleep_or_adc_threading_change(self):
        loop = body(FIRMWARE, "void loop()")
        self.assertNotIn("vTaskDelay", loop)
        self.assertNotIn("delay(10)", loop)
        self.assertNotIn("scale.beginTask", FIRMWARE)
        self.assertIn("scale.update()", FIRMWARE)
        self.assertIn("BUTTON_POLL_INTERVAL_MS = 2", FIRMWARE)
        self.assertIn("hdsIntervalElapsed(buttonNow, lastButtonPoll, BUTTON_POLL_INTERVAL_MS)", loop)
        self.assertNotIn("websocket.count()", loop)

    def test_compile_time_pm_environments_and_custom_build(self):
        self.assertIn("#define HDS_ENABLE_ENERGY_MENU HDS_FEATURE_ENERGY_MENU", HDS_FEATURES)
        self.assertIn("[env:esp32s3-pm-capable]", PLATFORMIO)
        self.assertIn(
            "custom_sdkconfig = file://sdkconfig.energy-menu.defaults",
            PLATFORMIO,
        )
        self.assertIn("[env:esp32s3-energy-menu]\nextends = env:esp32s3-pm-capable", PLATFORMIO)
        self.assertIn("[env:esp32s3-custom]\nextends = env:esp32s3", PLATFORMIO)
        self.assertIn(
            "[env:esp32s3-energy-menu-custom]\nextends = env:esp32s3-pm-capable",
            PLATFORMIO,
        )
        self.assertIn(
            '"energy-menu": ("HDS_FEATURE_ENERGY_MENU", ())',
            (ROOT / "tools" / "configure_custom_build.py").read_text(encoding="utf-8"),
        )
        self.assertIn("CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y", SDKCONFIG)
        self.assertIn("# CONFIG_BT_CTRL_LPCLK_SEL_RTC_SLOW is not set", SDKCONFIG)
        self.assertIn("CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y", SDKCONFIG)
        self.assertNotIn("CONFIG_BT_CTRL_LPCLK_SEL_RTC_SLOW=y", SDKCONFIG)

    def test_documented_scope(self):
        self.assertIn("defaults to off", DOCS)
        self.assertIn("does not add a normal-loop delay", DOCS)
        self.assertIn("event-driven ADC and button wake design is a follow-up", DOCS)
        self.assertNotIn("80-SPS", DOCS)


if __name__ == "__main__":
    unittest.main()
