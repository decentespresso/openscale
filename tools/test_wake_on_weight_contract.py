import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WOW = (ROOT / "include" / "wake_on_weight.h").read_text(encoding="utf-8")
POWER = (ROOT / "include" / "power.h").read_text(encoding="utf-8")
STORAGE = (ROOT / "include" / "storage.h").read_text(encoding="utf-8")
MENU = (ROOT / "include" / "menu.h").read_text(encoding="utf-8")
PARAMETER = (ROOT / "include" / "parameter.h").read_text(encoding="utf-8")
FIRMWARE = (ROOT / "src" / "hds.ino").read_text(encoding="utf-8")
DOCS = (ROOT / "docs" / "wake-on-weight.md").read_text(encoding="utf-8")
WOW_ADS = WOW[:WOW.index("#else")]


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
    raise AssertionError(signature)


class WakeOnWeightContractTests(unittest.TestCase):
    def test_feature_header_exists_with_rtc_state(self):
        self.assertIn("RTC_DATA_ATTR", WOW)
        self.assertIn("WOW_RTC_MAGIC", WOW)
        self.assertIn("wowIntervalUs", WOW)
        self.assertIn("WOW_TRIGGER_GRAMS", WOW)

    def test_micro_wake_uses_timer_wakeup_apis(self):
        self.assertIn("esp_sleep_enable_timer_wakeup", WOW)
        self.assertIn("esp_sleep_get_wakeup_cause", WOW)
        self.assertIn("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)", WOW)
        self.assertIn("esp_deep_sleep_start", WOW)

    def test_micro_wake_restores_80mhz_clock(self):
        self.assertIn("setCpuFrequencyMhz(80)", WOW)
        self.assertNotIn("setCpuFrequencyMhz(240)", WOW)
        self.assertNotIn(" 240", WOW.replace("0x", ""))

    def test_feature_never_touches_nvs_or_battery_in_micro_path(self):
        self.assertNotIn("storageGet", WOW)
        self.assertNotIn("Preferences", WOW)
        self.assertNotIn("analogRead", WOW)
        self.assertNotIn("ADS1115", WOW)

    def test_micro_path_stays_in_polling_mode(self):
        self.assertNotIn("beginTask", WOW)

    def test_no_marketing_language(self):
        self.assertNotIn("premium", WOW.lower())

    def test_rtc_attribute_lives_only_in_wow_header(self):
        for name, text in (("power.h", POWER), ("parameter.h", PARAMETER),
                           ("menu.h", MENU), ("hds.ino", FIRMWARE),
                           ("storage.h", STORAGE)):
            self.assertNotIn("RTC_DATA_ATTR", text, name)

    def test_sleep_chain_order_in_power_h(self):
        self.assertIn("wowCaptureBaselineForSleep();", POWER)
        self.assertIn("wowArmSleepTimer();", POWER)
        self.assertLess(POWER.index("wowCaptureBaselineForSleep();"),
                        POWER.index("scale.powerDown();"))
        self.assertLess(POWER.index("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)"),
                        POWER.index("wowArmSleepTimer();"))
        self.assertLess(POWER.index("wowArmSleepTimer();"),
                        POWER.index("esp_sleep_enable_ext1_wakeup_io"))

    def test_exactly_two_deep_sleep_call_sites(self):
        for name, text in (("POWER", POWER), ("WOW", WOW)):
            self.assertEqual(text.count("esp_deep_sleep_start()"), 1, name)

    def test_baseline_capture_gates(self):
        baseline = body(WOW_ADS, "void wowCaptureBaselineForSleep()")
        self.assertIn("i_wow_interval <= 0", baseline)
        self.assertIn("i_lowBatteryCount > 0", baseline)
        self.assertIn("CALIBRATION_VALUE_DEFAULT", baseline)
        self.assertIn("validSamples <= 0", baseline)
        self.assertIn("smoothedValue", baseline)

    def test_micro_wakeup_gates(self):
        micro = body(WOW_ADS, "void wowMicroWakeOrContinue()")
        self.assertIn("ESP_SLEEP_WAKEUP_TIMER", micro)
        self.assertIn("WOW_RTC_MAGIC", micro)
        self.assertIn("gpio_hold_dis", micro)
        self.assertIn("gpio_deep_sleep_hold_dis()", micro)
        self.assertIn("digitalWrite(PWR_CTRL, HIGH)", micro)
        self.assertIn("wowAdc.begin()", micro)
        self.assertIn("wowAdc.powerDown()", micro)
        self.assertIn("dataOutOfRange", micro)
        self.assertIn("wowRtc.intervalUs", micro)

    def test_boot_interceptor_slots_in_setup(self):
        self.assertLess(FIRMWARE.index("wowMicroWakeOrContinue();"),
                        FIRMWARE.index("storageInit()"))
        self.assertLess(FIRMWARE.index("wowMicroWakeOrContinue();"),
                        FIRMWARE.index("pinMode(PWR_CTRL, OUTPUT)"))
        self.assertLess(FIRMWARE.index("storageGetInt(KEY_WOW_INTERVAL, 0)"),
                        FIRMWARE.index("esp32_sleep();"))

    def test_setting_loaded_before_any_sleep_path(self):
        self.assertLess(FIRMWARE.index("i_wow_interval = storageGetInt(KEY_WOW_INTERVAL, 0)"),
                        FIRMWARE.index("releaseWakePinsFromRtcMode();"))

    def test_storage_key_in_all_three_paths(self):
        self.assertIn("KEY_WOW_INTERVAL = \"wow_interval\"", STORAGE)
        self.assertIn("KEY_WOW_INTERVAL", body(STORAGE, "bool storageHasAllSettings()"))
        self.assertIn("KEY_WOW_INTERVAL", body(STORAGE, "bool storageEnsureDefaults()"))
        self.assertIn("KEY_WOW_INTERVAL", body(STORAGE, "bool storageMigrateLegacyEeprom()"))

    def test_menu_row_and_cycle_action(self):
        self.assertIn("menuWakeOnWeight", MENU)
        self.assertIn("\"Wake: Off\"", MENU)
        self.assertIn("menuWakeOnWeightLabel, cycleWakeOnWeight, NULL, &menuPower", MENU)
        self.assertIn("&menuWakeOnWeight", body(MENU, "powerMenu[]"))
        cycle = body(MENU, "void cycleWakeOnWeight()")
        self.assertIn("WOW_INTERVAL_COUNT", cycle)
        self.assertIn("storagePutInt(KEY_WOW_INTERVAL, next)", cycle)
        self.assertIn("i_wow_interval = next", cycle)
        self.assertIn("updateWakeOnWeightLabel()", cycle)

    def test_refresh_menu_rows_updates_label(self):
        refresh = body(MENU, "void refreshMenuRows()")
        self.assertIn("updateWakeOnWeightLabel()", refresh)

    def test_globals_live_in_parameter_h(self):
        self.assertIn("int i_wow_interval = 0", PARAMETER)
        self.assertIn("int i_lowBatteryCount = 0", PARAMETER)
        self.assertIn("int i_lowBatteryCountTotal = 0", PARAMETER)

    def test_low_battery_increment_stays_in_power_h(self):
        sample = POWER[POWER.index("bool processNewBatterySample()"):]
        self.assertIn("i_lowBatteryCount++;", sample)
        self.assertIn("EnergyRuntimePolicy::lowBatteryConfirmed(i_lowBatteryCount)", sample)

    def test_docs_state_machine(self):
        self.assertIn("10 SPS", DOCS)
        self.assertIn("50 g", DOCS)
        self.assertIn("0.5 s", DOCS)
        self.assertIn("2 s", DOCS)
        self.assertIn("defaults to off", DOCS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
