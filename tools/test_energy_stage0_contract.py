import re
import unittest
from pathlib import Path
from test_grinder_feature_flag_contract import is_guarded_at


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
MENU_INTEGRATION = (ROOT / "include" / "menu.h").read_text(encoding="utf-8")
PARAMETER = (ROOT / "include" / "parameter.h").read_text(encoding="utf-8")
ENERGY_IDLE_WAKE = (ROOT / "include" / "energy_idle_wake.h").read_text(encoding="utf-8")
BLE = (ROOT / "include" / "ble.h").read_text(encoding="utf-8")
WEBSOCKET = (ROOT / "include" / "websocket.h").read_text(encoding="utf-8")
GYRO = (ROOT / "include" / "gyro.h").read_text(encoding="utf-8")
CUSTOM_BUILD = (ROOT / "tools" / "configure_custom_build.py").read_text(encoding="utf-8")


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


def energy_and_stock_branches(source):
    start = source.index("#if HDS_ENABLE_ENERGY_MENU")
    divider = source.index("#else", start)
    end = source.index("#endif", divider)
    return source[start:divider], source[divider:end]


class EnergyLightSleepContractTests(unittest.TestCase):
    def assert_guarded(self, source, text):
        matches = list(re.finditer(re.escape(text), source))
        self.assertTrue(matches, f"missing guarded contract: {text}")
        self.assertTrue(
            all(is_guarded_at(source, match.start(), "HDS_ENABLE_ENERGY_MENU") for match in matches),
            f"unguarded energy contract: {text}",
        )

    def test_feature_count_and_menu_order(self):
        self.assertEqual(
            ["SerialQuiet", "OledRedraw", "OledIdle", "LightSleep", "UsbSleepTest"],
            re.findall(r"^  (\w+),$", POLICY, re.MULTILINE),
        )
        self.assertEqual(
            [
                "Serial Quiet o", "OLED Redraw o", "OLED Idle o", "Light Sleep o",
                "USB Sleep Test o",
            ],
            re.findall(r'char menuEnergy\w+Label\[\] = "([^"]+)";', MENU),
        )
        self.assertNotIn("Motion Poll", MENU)
        self.assertNotIn("ACC Rail Off", MENU)
        self.assertNotIn("Power Cadence", MENU)
        self.assertNotIn("OLED Static", MENU)
        self.assertIn("static_cast<size_t>(EnergyFeature::Count)", MENU)

    def test_storage_migration_preserves_retained_features(self):
        self.assertIn("ENERGY_SCHEMA_VERSION = 8", STORAGE)
        self.assertIn('"e_light_sleep"', STORAGE)
        self.assertIn('"e_usb_sleep"', STORAGE)
        self.assertIn("KEY_ENERGY_MOTION_POLL", STORAGE)
        self.assertIn("KEY_ENERGY_ACC_RAIL_OFF", STORAGE)
        migration = body(STORAGE, "inline bool energyLoadSettings")
        self.assertIn("storageRemoveIfPresent(KEY_ENERGY_MOTION_POLL)", migration)
        self.assertIn("storageRemoveIfPresent(KEY_ENERGY_ACC_RAIL_OFF)", migration)
        self.assertIn("storageRemoveIfPresent(KEY_ENERGY_POWER_CADENCE)", migration)
        self.assertIn("storageRemoveIfPresent(KEY_ENERGY_OLED_STATIC)", migration)
        self.assertIn("getType(key) == PT_U8", STORAGE)

    def test_usb_sleep_test_uses_the_feature_store_reload_path(self):
        load = body(STORAGE, "inline bool energyLoadSettings")
        store = body(STORAGE, "inline bool energyStoreFeature")
        self.assertIn("EnergyFeature::Count", load)
        self.assertIn("ENERGY_FEATURE_KEYS[index]", load)
        self.assertIn("settings.select(feature, enabled)", load)
        self.assertIn("ENERGY_FEATURE_KEYS[index]", store)
        self.assertIn("EnergyFeature::Count", store)

    def test_power_cadence_is_always_enabled(self):
        self.assertNotIn("EnergyFeature::PowerCadence", FIRMWARE + SHUTDOWN)
        self.assertNotIn("processLegacyLowBattery", SHUTDOWN)
        self.assertIn("PowerCadenceState powerCadence", PARAMETER)
        cadence_offset = PARAMETER.index("PowerCadenceState powerCadence")
        self.assertFalse(is_guarded_at(PARAMETER, cadence_offset, "HDS_ENABLE_ENERGY_MENU"))
        battery = body(SHUTDOWN, "bool processNewBatterySample()")
        self.assertIn("powerCadence.batterySamples.shouldEvaluate", battery)
        self.assertIn("lowBatteryConfirmationSamples = 2", (ROOT / "include" / "energy_runtime_policy.h").read_text(encoding="utf-8"))
        loop = body(FIRMWARE, "void loop()")
        self.assertIn("powerCadence.chargeCheck.shouldRun(millis(), 200)", loop)
        wait = body(FIRMWARE, "unsigned long energyMainLoopWaitMs(unsigned long now)")
        self.assertIn("earlier(waitMs, 200)", wait)
        for signature in ["void power_off(int min)", "void power_off(double sec)"]:
            power_off = body(SHUTDOWN, signature)
            self.assertIn("processNewBatterySample()", power_off)
            self.assertNotIn("#if HDS_ENABLE_ENERGY_MENU", power_off)

    def test_runtime_transition_is_centralized(self):
        transition = body(FIRMWARE, "void applyEnergyFeatureTransition(EnergyFeature feature, bool wasEnabled, bool isEnabled) {")
        self.assertIn("feature == EnergyFeature::LightSleep", transition)
        self.assertIn("setEnergyLightSleepEnabled(isEnabled)", transition)
        self.assertIn("initEnergyPowerManagement()", FIRMWARE)
        self.assertIn("serviceEnergyPowerManagement()", FIRMWARE)
        self.assertIn("setEnergyPerformanceCritical(b_ota || b_pullOtaRunning)", FIRMWARE)

    def test_pm_lock_policy(self):
        self.assertIn("ESP_PM_CPU_FREQ_MAX", POWER)
        self.assertIn("ESP_PM_NO_LIGHT_SLEEP", POWER)
        self.assertIn("config.max_freq_mhz = 240", POWER)
        self.assertIn("config.min_freq_mhz = lightSleepEnabled ? 80 : 240", POWER)
        self.assertIn("config.light_sleep_enable = lightSleepEnabled", POWER)
        self.assertEqual(POWER.count("esp_pm_lock_create("), 4)
        self.assertEqual(POWER.count("esp_pm_lock_delete("), 1)
        self.assertIn("!usbSleepTestEnabled", POWER)
        self.assertIn("setUsbSleepTestEnabled", FIRMWARE)
        apply_setting = body(POWER, "bool applyLightSleepSetting(bool enabled)")
        self.assertLess(
            apply_setting.index("setLockHeld(stockNoLightSleepLock"),
            apply_setting.index("configure(true)"),
        )
        fail_closed = body(POWER, "bool failClosed()")
        self.assertIn("lightSleepEnabled = false", fail_closed)
        self.assertIn("setLockHeld(stockNoLightSleepLock", fail_closed)
        self.assertIn("setLockHeld(serialNoLightSleepLock", fail_closed)
        self.assertIn("configure(false)", fail_closed)
        update_locks = body(POWER, "bool updateLocks()")
        self.assertIn("performanceCritical", update_locks)
        self.assertLess(
            update_locks.index("stockSleepAcquireOk"),
            update_locks.index("stockCpuOk"),
        )
        deep_sleep = body(SHUTDOWN, "void esp32_sleep()")
        self.assertLess(
            deep_sleep.index("applyEnergyLightSleepSetting(false)"),
            deep_sleep.index("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)"),
        )
        self.assertLess(
            deep_sleep.index("setEnergyIdleWakeEnabled(false)"),
            deep_sleep.index("esp_sleep_enable_ext1_wakeup_io"),
        )
        self.assertLess(
            deep_sleep.index("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER)"),
            deep_sleep.index("esp_sleep_enable_ext1_wakeup_io"),
        )

    def test_event_driven_loop_wait(self):
        loop = body(FIRMWARE, "void loop()")
        self.assertNotIn("vTaskDelay", loop)
        self.assertNotIn("delay(10)", loop)
        self.assertNotIn("scale.beginTask", FIRMWARE)
        self.assertIn("scale.update()", FIRMWARE)
        self.assertIn("BUTTON_POLL_INTERVAL_MS = 2", FIRMWARE)
        self.assertIn("ulTaskNotifyTake(pdTRUE, waitTicks)", FIRMWARE)
        self.assertIn("digitalRead(SCALE_DOUT) == LOW", FIRMWARE)
        self.assertIn("serviceEnergyButtonGesture(buttonNow)", loop)
        self.assertIn("hdsIntervalElapsed(buttonNow, energyIdle.lastButtonPoll, BUTTON_POLL_INTERVAL_MS)", loop)
        self.assertNotIn("websocket.count()", loop)

    def test_light_sleep_off_keeps_polling_and_on_uses_wake_sources(self):
        buttons = body(FIRMWARE, "bool serviceEnergyButtonGesture(unsigned long now)")
        self.assertIn("if (!energyPolicy.featureEnabled(EnergyFeature::LightSleep)) return true", buttons)
        can_block = body(FIRMWARE, "bool energyMainLoopCanBlock()")
        self.assertIn("!energyLightSleepAllowedByUsbPolicy()", can_block)
        self.assertIn("energyUsbSleepTestActive()", can_block)
        self.assertIn("chargingBlocksSleep", can_block)
        wake_isr = body(ENERGY_IDLE_WAKE, "static void IRAM_ATTR energyMainLoopWakeIsr(void *context)")
        self.assertIn("vTaskNotifyGiveFromISR", wake_isr)
        self.assertNotIn("scale.", wake_isr)
        sleep_exit = body(
            ENERGY_IDLE_WAKE,
            "static esp_err_t IRAM_ATTR energyMainLoopWakeAfterLightSleep(int64_t, void *context)",
        )
        self.assertIn("wakePinsNeedRestore = true", sleep_exit)
        self.assertIn("xTaskNotifyGive(state->mainTask)", sleep_exit)
        self.assertNotIn("rtc_gpio_", sleep_exit)
        self.assertNotIn("ESP_ERROR_CHECK", ENERGY_IDLE_WAKE)
        restore = body(ENERGY_IDLE_WAKE, "static bool restoreEnergyLightSleepWakePins(uint64_t wakeMask)")
        for pin in ["SCALE_DOUT", "BUTTON_CIRCLE", "BUTTON_SQUARE", "USB_DET"]:
            self.assertIn(f"(gpio_num_t){pin}", restore)
        self.assertIn("wakeMask & energyLightSleepWakePinMask(pin)", restore)
        self.assertIn("esp_pm_light_sleep_register_cbs", ENERGY_IDLE_WAKE)
        self.assertIn("esp_sleep_enable_ext1_wakeup_io", ENERGY_IDLE_WAKE)
        self.assertIn("esp_sleep_disable_ext1_wakeup_io", ENERGY_IDLE_WAKE)
        for pin in ["SCALE_DOUT", "BUTTON_CIRCLE", "BUTTON_SQUARE", "USB_DET"]:
            self.assertIn(f"energyLightSleepWakePinMask((gpio_num_t){pin})", ENERGY_IDLE_WAKE)
        self.assertIn("rtc_gpio_is_valid_gpio(pin)", ENERGY_IDLE_WAKE)
        self.assertNotIn("energyLightSleepWakePinsSupported", ENERGY_IDLE_WAKE)
        self.assertIn(
            "attachInterruptArg(digitalPinToInterrupt(SCALE_DOUT), energyMainLoopWakeIsr",
            FIRMWARE,
        )
        self.assertIn("attachInterruptArg(digitalPinToInterrupt(BUTTON_CIRCLE)", FIRMWARE)
        self.assertIn("attachInterruptArg(digitalPinToInterrupt(BUTTON_SQUARE)", FIRMWARE)

    def test_soft_sleep_blocks_without_adc_wake(self):
        can_block = body(FIRMWARE, "bool energyMainLoopCanBlock()")
        pending = body(FIRMWARE, "bool energyMainLoopHasPendingWork()")
        wake_setup = body(FIRMWARE, "bool setEnergyIdleWakeEnabled(bool enabled)")
        self.assertNotIn("b_softSleep ||", can_block)
        self.assertIn("!b_softSleep && digitalRead(SCALE_DOUT) == LOW", pending)
        self.assertIn("const bool scaleWakeEnabled = !b_softSleep", wake_setup)
        required_mask = body(
            ENERGY_IDLE_WAKE,
            "static uint64_t energyLightSleepRequiredWakePinMask(bool scaleWakeEnabled,",
        )
        self.assertIn("if (scaleWakeEnabled)", required_mask)
        wait = body(FIRMWARE, "unsigned long energyMainLoopWaitMs(unsigned long now)")
        self.assertIn("!b_softSleep && energyIdle.scaleWakeFallback", wait)
        self.assertIn("refreshEnergyIdleWakeForRuntimeState()", BLE + WEBSOCKET + FIRMWARE)

    def test_unsupported_wake_sources_use_bounded_fallbacks(self):
        wake_setup = body(FIRMWARE, "bool setEnergyIdleWakeEnabled(bool enabled)")
        for fallback in [
            "scaleWakeFallback",
            "circleButtonWakeFallback",
            "squareButtonWakeFallback",
            "usbWakeFallback",
        ]:
            self.assertIn(fallback, wake_setup)
        wait = body(FIRMWARE, "unsigned long energyMainLoopWaitMs(unsigned long now)")
        self.assertIn("scale.getConversionTime()", wait)
        self.assertIn("ADS1232_DEFAULT_CONVERSION_MS", wait)
        scale_fallback = wait[
            wait.index("if (!b_softSleep && energyIdle.scaleWakeFallback)"):
            wait.index("if (energyIdle.circleButtonWakeFallback")
        ]
        self.assertRegex(
            scale_fallback,
            r"timeUntil\(\s*now,\s*energyIdle\.lastScaleData,\s*max\(1UL, scalePollMs\)\)",
        )
        self.assertNotIn("earlier(waitMs, max(1UL, scalePollMs))", scale_fallback)
        self.assertIn("BUTTON_POLL_INTERVAL_MS", wait)
        self.assertIn("ENERGY_USB_FALLBACK_POLL_MS", wait)
        self.assertIn("waitMs = EnergyRuntimePolicy::earlier", wait)
        required_mask = body(
            ENERGY_IDLE_WAKE,
            "static uint64_t energyLightSleepRequiredWakePinMask(bool scaleWakeEnabled,",
        )
        self.assertIn("energyLightSleepWakePinMask", required_mask)
        self.assertNotIn("return false", required_mask)

    def test_light_sleep_transition_and_boot_fail_closed(self):
        toggle = body(MENU, "void toggleEnergyLightSleep()")
        enable_runtime = toggle.index("setEnergyLightSleepEnabled(true)")
        enable_store = toggle.index("energyStoreFeature(EnergyFeature::LightSleep, true)")
        self.assertLess(enable_runtime, enable_store)
        transition = body(FIRMWARE, "bool setEnergyLightSleepEnabled(bool enabled)")
        self.assertIn("setEnergyIdleWakeEnabled(false)", transition)
        self.assertIn("applyEnergyLightSleepSetting(false)", transition)
        setup = body(FIRMWARE, "void setup()")
        self.assertIn("Stored Light Sleep rejected; disabled", setup)
        self.assertIn("energyStoreFeature(EnergyFeature::LightSleep, false)", setup)
        self.assertNotIn("while (1)", setup[setup.index("initEnergyPowerManagement()") : setup.index("energyPolicy.begin")])

    def test_deferred_work_wakes_main_loop(self):
        for signature in [
            "inline void remoteQueuePending(uint32_t bits)",
            "inline void remoteReplacePending(uint32_t setBits, uint32_t clearBits)",
            "inline void remoteQueueSamplesInUse(uint8_t samplesInUse)",
        ]:
            self.assertIn("notifyEnergyMainLoop()", body(WEBSOCKET, signature))
        self.assertIn("notifyEnergyMainLoop()", body(PARAMETER, "void requestRemoteTare()"))
        self.assertIn("notifyEnergyMainLoop()", body(BLE, "void queueBleStatusResponse()"))
        self.assertIn("notifyEnergyMainLoop()", body(BLE, "void queueBleVoltageResponse()"))

    def test_oled_redraw_signature_matches_rendering(self):
        signature = body(FIRMWARE, "uint32_t energyDisplaySignature(unsigned long now)")
        self.assertIn("stopWatch.elapsed()", signature)
        self.assertNotIn("stopWatch.elapsed() / 1000", signature)
        self.assertIn("EnergyRuntimePolicy::batteryDisplayBucket(batteryPercent)", signature)
        self.assertNotIn("static_cast<uint32_t>(batteryPercent)", signature)
        self.assertIn("WiFi.getMode()", signature)
        self.assertIn("f_displayedValue > OVER_WEIGHT", signature)
        self.assertIn("f_displayedValue <= -1000.0f", signature)
        self.assertIn("const bool bleConnected = bleHasLiveClient()", signature)
        self.assertNotIn("deviceConnected", signature)

        draw_ble = body(FIRMWARE, "void drawBle(unsigned long now)")
        self.assertIn("(now / 1000) & 1", draw_ble)
        battery_start = FIRMWARE.index("void drawBattery(unsigned long now)")
        draw_battery = FIRMWARE[battery_start:FIRMWARE.index("void drawAbout()", battery_start)]
        self.assertIn("(now / 500) & 1", draw_battery)
        self.assertIn("EnergyRuntimePolicy::batteryDisplayBucket", draw_battery)
        self.assertNotIn("b_drawBle", FIRMWARE)
        self.assertNotIn("t_ble_box", FIRMWARE)
        self.assertNotIn("b_showBatteryIcon", FIRMWARE + PARAMETER)
        self.assertNotIn("t_batteryIcon", FIRMWARE + PARAMETER)

        update = body(FIRMWARE, "void updateOled()")
        self.assertIn("energyDisplaySignature(frameNow)", update)
        self.assertIn("drawBattery(frameNow)", update)
        self.assertIn("drawBle(frameNow)", update)
        self.assertIn("energyRuntime.displayMode == DisplayIdleMode::Off", update)
        self.assertNotIn("EnergyFeature::OledStatic", update)

    def test_oled_boot_state_and_idle_policy(self):
        setup = body(FIRMWARE, "void setup()")
        oled_begin = setup.index("u8g2.begin()")
        self.assertLess(oled_begin, setup.index("b_u8g2Sleep = false", oled_begin))
        self.assertIn("energyPolicy.begin(millis())", setup)
        housekeeping = body(FIRMWARE, "void serviceEnergyHousekeeping(unsigned long now)")
        self.assertIn("EnergyFeature::OledIdle", housekeeping)
        self.assertIn("processEnergyActivities(now)", housekeeping)
        self.assertNotIn("OledStatic", FIRMWARE + POLICY + MENU)

    def test_adc_recovery_and_serial_policy_remain_reachable(self):
        wait = body(FIRMWARE, "unsigned long energyMainLoopWaitMs(unsigned long now)")
        self.assertIn("ENERGY_ADC_SIGNAL_TIMEOUT_MS", wait)
        self.assertIn("ENERGY_ADC_RECOVERY_START_MS", wait)
        self.assertIn("ENERGY_ADC_RECOVERY_RETRY_MS", wait)
        self.assertIn("setSerialTransportActive(energySerialTransportActive())", FIRMWARE)
        self.assertIn("lightSleepEnabled && serialTransportActive && !usbSleepTestEnabled", POWER)
        service = body(FIRMWARE, "void serviceEnergyPowerManagement()")
        self.assertIn("setEnergyLightSleepEnabled(false)", service)
        self.assertIn("energyStoreFeature(EnergyFeature::LightSleep, false)", service)
        self.assertIn("failureLogged", service)
        self.assertNotIn("esp_sleep_enable_uart_wakeup", FIRMWARE + POWER)
        self.assertNotIn("uart_set_wakeup_threshold", FIRMWARE + POWER)
        self.assertNotIn("80-SPS", FIRMWARE + DOCS)

    def test_compile_time_pm_environments_and_custom_build(self):
        self.assertIn("#define HDS_FEATURE_ENERGY_MENU 0", HDS_FEATURES)
        self.assertIn("#define HDS_ENABLE_ENERGY_MENU HDS_FEATURE_ENERGY_MENU", HDS_FEATURES)
        normal = PLATFORMIO.split("[env:esp32s3]", 1)[1].split("[env:esp32s3-grinder]", 1)[0]
        pm_capable = PLATFORMIO.split("[env:esp32s3-pm-capable]", 1)[1].split("[env:esp32s3-energy-menu]", 1)[0]
        energy = PLATFORMIO.split("[env:esp32s3-energy-menu]", 1)[1].split("[env:esp32s3-custom]", 1)[0]
        custom = PLATFORMIO.split("[env:esp32s3-custom]", 1)[1].split("[env:esp32s3-energy-menu-custom]", 1)[0]
        energy_custom = PLATFORMIO.split("[env:esp32s3-energy-menu-custom]", 1)[1].split("[env:native]", 1)[0]
        self.assertNotIn("sdkconfig.energy-menu.defaults", normal)
        self.assertNotIn("HDS_ENABLE_ENERGY_MENU=1", normal)
        self.assertIn("extends = env:esp32s3", pm_capable)
        self.assertIn("custom_sdkconfig = file://sdkconfig.energy-menu.defaults", pm_capable)
        self.assertIn("extends = env:esp32s3-pm-capable", energy)
        self.assertIn("-DHDS_ENABLE_ENERGY_MENU=1", energy)
        self.assertIn("extends = env:esp32s3", custom)
        self.assertNotIn("sdkconfig.energy-menu.defaults", custom)
        self.assertIn("extends = env:esp32s3-pm-capable", energy_custom)
        self.assertIn(
            '"energy-menu": ("HDS_FEATURE_ENERGY_MENU", (), FIRMWARE_REFS)',
            CUSTOM_BUILD,
        )
        self.assertIn('DEFAULT_FEATURES = {', CUSTOM_BUILD)
        self.assertNotIn('DEFAULT_FEATURES = set(FEATURES)', CUSTOM_BUILD)
        self.assertIn("CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y", SDKCONFIG)
        self.assertIn("CONFIG_PM_LIGHT_SLEEP_CALLBACKS=y", SDKCONFIG)
        self.assertIn("# CONFIG_BT_CTRL_LPCLK_SEL_RTC_SLOW is not set", SDKCONFIG)
        self.assertIn("CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y", SDKCONFIG)
        self.assertNotIn("CONFIG_BT_CTRL_LPCLK_SEL_RTC_SLOW=y", SDKCONFIG)

    def test_energy_runtime_is_compile_gated(self):
        for source, text in [
            (MENU_INTEGRATION, '#include "energy_menu.h"'),
            (MENU_INTEGRATION, "&menuEnergy"),
            (PARAMETER, '#include "energy_power_management.h"'),
            (PARAMETER, "EnergyPolicy energyPolicy"),
            (PARAMETER, "EnergyPowerManagement energyPowerManagement"),
            (FIRMWARE, "serviceEnergyPowerManagement();"),
            (FIRMWARE, "energyPowerManagement.service("),
            (FIRMWARE, "energyPolicy.featureEnabled("),
            (FIRMWARE, "energyLightSleepAllowedByUsbPolicy()"),
            (FIRMWARE, "applyEnergyLightSleepSetting("),
            (FIRMWARE, "waitForEnergyMainLoopWork();"),
            (FIRMWARE, "ulTaskNotifyTake(pdTRUE, waitTicks);"),
            (PARAMETER, "EnergyIdleState energyIdle"),
            (SHUTDOWN, "energyPolicy.featureEnabled("),
            (SHUTDOWN, "esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);"),
            (BLE, "remoteReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON | WSP_DISPLAY_OFF);"),
            (WEBSOCKET, "wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON | WSP_DISPLAY_OFF);"),
            (GYRO, "double readGyroZPhysical()"),
        ]:
            self.assert_guarded(source, text)

    def test_stock_transport_gyro_and_power_paths_are_preserved(self):
        ble_energy, ble_stock = energy_and_stock_branches(body(BLE, "void softSleepOff()"))
        self.assertNotIn("b_softSleep = false", ble_energy)
        self.assertIn("const bool wasSoftSleep = b_softSleep", ble_stock)
        self.assertIn("remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF)", ble_stock)

        ws_start = WEBSOCKET.index('Serial.println("Websocket soft sleep off detected.");')
        ws_end = WEBSOCKET.index('sendWebsocketStatus(client, "ok");', ws_start)
        ws_energy, ws_stock = energy_and_stock_branches(WEBSOCKET[ws_start:ws_end])
        self.assertNotIn("b_softSleep = false", ws_energy)
        self.assertIn("const bool wasSoftSleep = b_softSleep", ws_stock)
        self.assertIn("wsReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF)", ws_stock)

        self.assertEqual(2, GYRO.count("#else\ndouble gyro_z()"))

        for signature in ["void power_off(int min)", "void power_off(double sec)"]:
            power_off = body(SHUTDOWN, signature)
            self.assertNotIn("#if HDS_ENABLE_ENERGY_MENU", power_off)
            self.assertIn("processNewBatterySample()", power_off)

        exit_menu = body(MENU_INTEGRATION, "void exitMenu() {")
        self.assertEqual(1, exit_menu.count("invalidateMenuFrame();"))

    def test_documented_scope(self):
        self.assertIn("defaults to off", DOCS)
        self.assertIn("does not add a fixed normal-loop delay", DOCS)
        self.assertIn("blocks until ADS1232 data ready", DOCS)
        self.assertIn("first serial byte", DOCS)
        self.assertIn("Power cadence is always active", DOCS)
        self.assertIn("OLED Idle", DOCS)
        self.assertIn("USB Sleep Test", DOCS)
        self.assertIn("remains effective after a USB power-cycle or reboot", DOCS)
        self.assertIn("only that source uses bounded polling", DOCS)
        self.assertIn("SCALE_DOUT is neither an EXT1 source nor a fallback polling requirement", DOCS)
        self.assertNotIn("80-SPS", DOCS)


if __name__ == "__main__":
    unittest.main()
