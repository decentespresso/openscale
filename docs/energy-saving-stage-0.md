# Energy Saving Runtime Policy

The Energy Saving menu is compiled by `HDS_ENABLE_ENERGY_MENU=1`. The dedicated `esp32s3-energy-menu` environment and custom builds selecting `energy-menu` use the PM-capable environment. Stock `esp32s3` remains unchanged.

The menu contains `Back`, `Serial Quiet`, `Power Cadence`, `OLED Redraw`, `OLED Idle`, `OLED Static`, and `Light Sleep`. All settings are persistent, and `Light Sleep` defaults to off.

Energy schema version 5 preserves the five existing settings, discards the old `Motion Poll` and `ACC Rail Off` values, and creates `Light Sleep` as off. Invalid or missing Boolean values use their safe defaults, and the migration writes its marker only after all writes and removals succeed.

With `Light Sleep` off, `ESP_PM_CPU_FREQ_MAX` and `ESP_PM_NO_LIGHT_SLEEP` are held. With it on, those global locks are released. ESP-IDF driver locks remain authoritative. OTA is the only current explicit performance-critical state; its CPU lock is managed from the main loop.

The runtime controller creates each PM lock once and updates lock ownership on menu transitions. It does not add a fixed normal-loop delay. With `Light Sleep` off, the existing polling loop and 2 ms button cadence remain unchanged.

With `Light Sleep` on during normal live weighing, the Arduino loop task blocks until ADS1232 data ready, a button press, deferred main-loop work, or the next required software deadline. ADC reads and button handling stay in the main-loop task. Active button gestures retain the 2 ms cadence.

USB presence keeps the existing serial PM lock held and disables main-loop blocking. Boards without USB detection use the same conservative policy continuously. UART wake and a wake preamble are not used, so the first serial byte is not exposed to a light-sleep wake threshold.

Explicit soft sleep and deep sleep retain their existing behavior. Soft sleep does not cause the PM performance lock to remain held.
