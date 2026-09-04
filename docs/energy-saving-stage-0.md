# Energy Saving Runtime Policy

The Energy Saving menu is compiled by `HDS_ENABLE_ENERGY_MENU=1`. The dedicated `esp32s3-energy-menu` environment and custom builds selecting `energy-menu` use the PM-capable environment.

The menu contains `Back`, `OLED Redraw`, `OLED Idle`, `Light Sleep`, and `USB Sleep Test`. All settings are persistent and each setting defaults to off.

Energy schema version 9 preserves the four retained settings and discards the old `Serial Quiet`, `Power Cadence`, `OLED Static`, `Motion Poll`, and `ACC Rail Off` values. Invalid or missing Boolean values use their safe defaults, and the migration writes its marker only after all writes and removals succeed.

Power cadence is always active in stock and Energy Menu builds. Auto-off evaluation runs once per second, charging checks run every 200 ms, and low-battery shutdown requires two distinct battery samples.

With `Light Sleep` off, `ESP_PM_CPU_FREQ_MAX` and `ESP_PM_NO_LIGHT_SLEEP` are held. With it on, those global locks are released. ESP-IDF driver locks remain authoritative. OTA is the only current explicit performance-critical state; its CPU lock is managed from the main loop.

The runtime controller creates each PM lock once and updates lock ownership on menu transitions. It does not add a fixed normal-loop delay. With `Light Sleep` off, the existing polling loop and 2 ms button cadence remain unchanged.

With `Light Sleep` on during normal live weighing, the Arduino loop task blocks until ADS1232 data ready, a button press, deferred main-loop work, or the next required software deadline. ADC reads and button handling stay in the main-loop task. Active button gestures retain the 2 ms cadence. V8.1 uses hardware wake for its selected scale, button, and USB pins. If another selected pin is not RTC-wake capable, only that source uses bounded polling while supported sources remain event-driven. That fallback can consume more power, and older hardware revisions have not been physically validated.

USB presence keeps the existing serial PM lock held and disables main-loop blocking. `USB Sleep Test` is persistent, defaults to off, and overrides the normal USB and charging no-Light-Sleep safeguards only while `Light Sleep` is on. It is intended for repeatable USB-powered current measurements, remains effective after a USB power-cycle or reboot, and does not guarantee USB serial reliability. Boards without USB detection use the same conservative policy unless the override is enabled. UART wake and a wake preamble are not used, so the first serial byte is not exposed to a light-sleep wake threshold.

With BLE connected, `Light Sleep` on, USB policy permitting sleep, and explicit soft sleep active, the Arduino loop uses the same event-driven wait and automatic ESP32-S3 Light Sleep remains available. BLE deferred commands wake main-loop processing. SCALE_DOUT is neither an EXT1 source nor a fallback polling requirement while the powered-down ADC is in explicit soft sleep.

Explicit soft sleep and deep sleep retain their existing behavior. Soft sleep does not cause the PM performance lock to remain held.
