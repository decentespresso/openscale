# Energy Saving Runtime Policy

The Energy Saving menu is compiled by `HDS_ENABLE_ENERGY_MENU=1`. The dedicated `esp32s3-energy-menu` environment and custom builds selecting `energy-menu` use the PM-capable environment.

The menu contains `Back`, `OLED Redraw`, `OLED Idle`, `Light Sleep`, and `USB Sleep Test`. All settings are persistent and each setting defaults to off. The `Light Sleep` row toggles between `o` (off) and `x` (on). Enabling it selects the aggressive Light Sleep+ implementation; there is no separate `+` menu state. The responsive implementation remains in code. Previously saved responsive settings remain effective until Light Sleep is toggled off and on.

Energy schema version 10 preserves the four visible settings and adds the hidden `LightSleepPlus` profile bit. Devices upgrading directly from schemas 5 through 9 with Light Sleep enabled migrate to Light Sleep+ so the previously selected power behavior is preserved. Schema 5 introduced `e_light_sleep`; schemas 5 through 9 used aggressive semantics. An existing profile bit is preserved. Invalid or missing Boolean values use their safe defaults, and the migration writes its marker only after all writes and removals succeed.

Power cadence is always active in stock and Energy Menu builds. Auto-off evaluation runs once per second, charging checks run every 200 ms, and low-battery shutdown requires two distinct battery samples.

With `Light Sleep` off, `ESP_PM_CPU_FREQ_MAX` and `ESP_PM_NO_LIGHT_SLEEP` are held. With either Light Sleep profile on, those global locks are normally released. ESP-IDF driver locks remain authoritative. OTA remains performance-critical.

The runtime controller creates each PM lock once and updates lock ownership on menu transitions. It does not add a fixed normal-loop delay.

Both Light Sleep profiles boost physical button interaction. A hardware button wake acquires the existing performance-critical PM path immediately after RTC wake-pin restore and is then sampled before unrelated WebSocket, BLE-response, housekeeping, and serial work. While a physical button gesture is active, and for 50 ms after the last observed press level, the same PM path holds max CPU frequency and prevents automatic Light Sleep. The menu-selected Light Sleep+ profile uses this boost without waiting for debounce to finish.

Before persisted Light Sleep is enabled, both AceButton instances sample the physical inputs across their configured debounce interval. Released buttons establish a HIGH baseline; a button held through boot establishes LOW without a synthetic press, and gesture polling remains active to observe its release. Responsive button wakes continue polling through the configured debounce interval before processing deferred transport work, stopping on confirmation, release, suppression, or a deadline of the debounce interval plus one poll interval. Light Sleep+ only takes the initial sample.

Light Sleep+ keeps the event-driven idle duty cycle from the previous Light Sleep implementation, with the short button boost described above. The 200 ms maximum main-loop block deadline is unchanged in both profiles.

The `wakeinfo` USB text command reports the active Light Sleep profile and retained button-wake diagnostics: the latest EXT1 mask, button wake count, time from the recorded button wake to the first forced button poll, and time to the first AceButton pressed event. Diagnostics are retained in RAM and printed only on request.

During normal live weighing, the Arduino loop task blocks until ADS1232 data ready, a button press, deferred main-loop work, or the next required software deadline. ADC reads stay in the existing `pureScale()` path. This change does not add a weight-triggered performance boost or extra ADC polling; weight responsiveness should be evaluated separately against power measurements before adding one.

V8.1 uses hardware wake for its selected scale, button, and USB pins. If another selected pin is not RTC-wake capable, only that source uses bounded polling while supported sources remain event-driven. That fallback can consume more power, and older hardware revisions have not been physically validated.

USB presence keeps the existing serial PM lock held and disables main-loop blocking. `USB Sleep Test` is persistent, defaults to off, and overrides the normal USB and charging no-Light-Sleep safeguards only while Light Sleep is on. It is intended for repeatable USB-powered current measurements, remains effective after a USB power-cycle or reboot, and does not guarantee USB serial reliability. Boards without USB detection use the same conservative policy unless the override is enabled. UART wake and a wake preamble are not used, so the first serial byte is not exposed to a light-sleep wake threshold.

With BLE connected, Light Sleep on, USB policy permitting sleep, and explicit soft sleep active, the Arduino loop uses the same event-driven wait and automatic ESP32-S3 Light Sleep remains available. BLE deferred commands wake main-loop processing. SCALE_DOUT is neither an EXT1 source nor a fallback polling requirement while the powered-down ADC is in explicit soft sleep.

Explicit soft sleep and deep sleep retain their existing behavior. Soft sleep does not cause the PM performance lock to remain held.

After installing the pinned build dependencies, run `python tools/check_energy_button_regressions.py`. It compiles the firmware button and migration functions against AceButton 1.10.1 with host-side GPIO, clock, and NVS doubles. Use `--acebutton <library-directory>` to select another local installation of that version. Physical first-press, tare, timer, double-click, long-press, and power measurements still require device validation.
