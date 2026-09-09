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
WOW_ADS = WOW[:WOW.index("#else  // !ADS1232ADC")]


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
        self.assertIn("RTC_DATA_ATTR WowRtcState wowRtc", PARAMETER)
        self.assertIn("WOW_RTC_MAGIC", WOW)
        self.assertIn("WOW_TRIGGER_GRAMS", WOW)
        self.assertIn("WOW_INTERVAL_COUNT", WOW)
        self.assertIn("2000000", WOW)
        self.assertIn("3000000", WOW)
        self.assertIn("4000000", WOW)
        self.assertNotIn("500000", WOW)
        self.assertNotIn("1000000", WOW)

    def test_micro_wake_uses_timer_wakeup_apis(self):
        self.assertIn("esp_sleep_enable_timer_wakeup", WOW)
        self.assertIn("esp_sleep_get_wakeup_cause", WOW)
        self.assertIn("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)", WOW)
        self.assertIn("esp_deep_sleep_start", WOW)

    def test_micro_wake_downclocks_to_minimum_clock(self):
        self.assertIn("wowSetCpuFrequencyMhz(20)", WOW)
        self.assertNotIn("setCpuFrequencyMhz(80)", WOW)
        self.assertNotIn("setCpuFrequencyMhz(240)", WOW)
        clock = body(WOW_ADS, "static void wowSetCpuFrequencyMhz(")
        self.assertIn("#ifdef CONFIG_PM_ENABLE", clock)
        self.assertNotIn("setCpuFrequencyMhz(", clock[:clock.index("#else")])

    def test_micro_wake_never_touches_nvs_or_battery(self):
        self.assertNotIn("storageGet", WOW)
        self.assertNotIn("Preferences", WOW)
        self.assertNotIn("analogRead", WOW)
        self.assertNotIn("ADS1115", WOW)

    def test_micro_path_stays_in_polling_mode(self):
        self.assertNotIn("beginTask", WOW)

    def test_no_marketing_language(self):
        self.assertNotIn("premium", WOW.lower())

    def test_rtc_attribute_lives_only_in_parameter_header(self):
        for name, text in (("power.h", POWER), ("wake_on_weight.h", WOW),
                           ("menu.h", MENU), ("hds.ino", FIRMWARE),
                           ("storage.h", STORAGE)):
            self.assertNotIn("RTC_DATA_ATTR", text, name)

    def test_sleep_chain_order_in_power_h(self):
        self.assertIn("wowCaptureBaselineForSleep();", POWER)
        self.assertIn("wowArmSleepTimer();", POWER)
        self.assertLess(POWER.index("wowCaptureBaselineForSleep();"),
                        POWER.index("wowArmSleepTimer();"))
        self.assertLess(POWER.index("wowArmSleepTimer();"),
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
        self.assertIn("fabsf(f_calibration_value)", baseline)
        self.assertIn("wowRtc.intervalUs = wowIntervalUs", baseline)

    def test_baseline_rejects_invalid_snapshot_before_arming(self):
        baseline = body(WOW_ADS, "void wowCaptureBaselineForSleep()")
        guard = "if (info.validSamples <= 0 || info.dataOutOfRange || info.signalTimeout) return;"
        self.assertEqual(baseline.count("scale.getDebugInfo()"), 1)
        self.assertIn("const auto info = scale.getDebugInfo();", baseline)
        self.assertIn("wowRtc.baselineRaw = info.smoothedValue;", baseline)
        self.assertLess(baseline.index("wowRtc.armed = 0;"), baseline.index(guard))
        self.assertLess(baseline.index(guard), baseline.index("wowRtc.armed = 1;"))
        self.assertLess(baseline.index(guard), baseline.index("wowRtc.baselineRaw ="))

    def test_micro_wakeup_gates(self):
        micro = body(WOW_ADS, "void wowMicroWakeOrContinue()")
        self.assertIn("getCpuFrequencyMhz()", micro)
        self.assertIn("wowSetCpuFrequencyMhz(bootFreqMhz)", micro)
        self.assertIn("ESP_SLEEP_WAKEUP_TIMER", micro)
        self.assertIn("WOW_RTC_MAGIC", micro)
        self.assertIn("gpio_hold_dis", micro)
        self.assertIn("gpio_deep_sleep_hold_dis()", micro)
        self.assertIn("digitalWrite(PWR_CTRL, HIGH)", micro)
        self.assertIn("wowAdc.begin()", micro)
        self.assertIn("wowAdc.powerDown()", micro)
        self.assertIn("!outOfRange", micro)
        self.assertIn("esp_sleep_enable_timer_wakeup(wowRtc.intervalUs)", micro)

    def test_boot_interceptor_slots_in_setup(self):
        setup = body(FIRMWARE, "void setup()")
        self.assertLess(setup.index("wowMicroWakeOrContinue();"), setup.index("Serial.begin"))
        self.assertLess(FIRMWARE.index("wowMicroWakeOrContinue();"),
                        FIRMWARE.index("storageInit()"))
        self.assertLess(FIRMWARE.index("wowMicroWakeOrContinue();"),
                        FIRMWARE.index("pinMode(PWR_CTRL, OUTPUT)"))
        self.assertLess(FIRMWARE.index("storageGetInt(KEY_WOW_INTERVAL, 0)"),
                        FIRMWARE.index("esp32_sleep();"))
        self.assertLess(FIRMWARE.index("i_wow_interval = storageGetInt(KEY_WOW_INTERVAL, 0)"),
                        FIRMWARE.index("releaseWakePinsFromRtcMode();"))

    def test_storage_key_in_all_three_paths(self):
        self.assertIn("KEY_WOW_INTERVAL = \"wow_interval\"", STORAGE)
        self.assertIn("KEY_WOW_INTERVAL", body(STORAGE, "bool storageHasAllSettings()"))
        self.assertIn("KEY_WOW_INTERVAL", body(STORAGE, "bool storageEnsureDefaults()"))
        self.assertIn("KEY_WOW_INTERVAL", body(STORAGE, "bool storageMigrateLegacyEeprom()"))

    def test_menu_row_and_cycle_action(self):
        self.assertIn("menuWakeOnWeight", MENU)
        self.assertIn("menuWakeOnWeightLabel, cycleWakeOnWeight, NULL, &menuPower", MENU)
        self.assertIn("&menuWakeOnWeight", body(MENU, "powerMenu[]"))
        cycle = body(MENU, "void cycleWakeOnWeight()")
        self.assertIn("WOW_INTERVAL_COUNT", cycle)
        self.assertIn("storagePutInt(KEY_WOW_INTERVAL, next)", cycle)
        self.assertIn("i_wow_interval = next", cycle)
        self.assertIn("updateWakeOnWeightLabel()", cycle)

    def test_menu_label_uses_cycle_pattern(self):
        self.assertIn("char menuWakeOnWeightLabel[24] = \"WakeOnWeight o\"", MENU)
        labels = body(MENU, "void updateWakeOnWeightLabel()")
        self.assertIn("\"o\"", labels)
        self.assertIn("\"2\"", labels)
        self.assertIn("\"3\"", labels)
        self.assertIn("\"4\"", labels)

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
        self.assertIn("2 s", DOCS)
        self.assertIn("3 s", DOCS)
        self.assertIn("4 s", DOCS)
        self.assertIn("defaults to off", DOCS)

    def test_button_polling_and_latched_boot(self):
        for signature in ("static bool wowWaitForRail()", "static bool wowReadOneSample("):
            self.assertIn("wowPhysicalWakeRequested()", body(WOW_ADS, signature))
        micro = body(WOW_ADS, "void wowMicroWakeOrContinue()")
        self.assertGreaterEqual(micro.count("wowPhysicalWakeRequested()"), 3)
        self.assertIn("rtc_gpio_get_level", WOW)
        self.assertIn("if (wowButtonWake) break;", body(FIRMWARE, "void setup()"))
        self.assertNotIn("Serial.", WOW)

    def test_adc_cleanup_precedes_decision(self):
        micro = body(WOW_ADS, "void wowMicroWakeOrContinue()")
        self.assertLess(micro.index("wowAdc.powerDown()"), micro.index("wowAdc.end()"))
        self.assertLess(micro.index("wowAdc.end()"), micro.index("if (gotSample"))

    def test_session_fallbacks_and_sleep_interval(self):
        self.assertIn("WOW_MAX_TICKS = 900", WOW)
        self.assertIn("WOW_MAX_FAILURES = 5", WOW)
        self.assertIn("wowRtc.consecutiveFailures = 0", WOW)
        self.assertIn("if (wowRtc.armed) esp_sleep_enable_timer_wakeup", WOW)
        self.assertIn('"Sleep 2s", "Sleep 3s", "Sleep 4s"', MENU)
        self.assertIn("sleep intervals", DOCS)
        self.assertIn("900-tick", DOCS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
