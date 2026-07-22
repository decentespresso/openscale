# AI GPIO Notes

Read this before changing board pins, deep-sleep wake pins, sleep holds, RTC GPIO use, USB/JTAG pins, strapping pins, or `pinMode`/`digitalWrite`/`digitalRead` behavior.

Source files are `include/config.h`, `include/power.h`, and `src/hds.ino`. Use the official Espressif ESP32-S3 GPIO guide as the source of truth when pin behavior matters:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html

## Fast Check

Before editing pins, search all users:

```sh
rg -n "pinMode|digitalWrite|digitalRead|gpio_|rtc_gpio|gpio_hold|GPIO_NUM|BUTTON_|PWR_CTRL|SCALE_|OLED_|USB_DET|WAKE" include src platformio.ini
```

Trace sleep, wake, boot, USB, OLED, load-cell, WiFi/BLE, and power-gating flows that touch the pin. Fix shared setup paths, not just the reported caller.

## ESP32-S3 Rules

- Physical GPIOs are `0-21` and `26-48`.
- Strapping pins are `GPIO0`, `GPIO3`, `GPIO45`, and `GPIO46`.
- `GPIO26-GPIO32` are normally SPI flash/PSRAM pins and should not be used.
- `GPIO33-GPIO37` can also be flash/PSRAM pins on ESP32-S3R8/R8V style modules.
- `GPIO19` and `GPIO20` are USB-JTAG by default.
- Deep-sleep wake pins must work as RTC GPIOs.
- `gpio_config()`, `gpio_set_direction()`, and `gpio_set_pull_mode()` overwrite existing pin config.
- `gpio_get_level()` returns `0` when the pad is not configured for input.
- Do not rely on reset defaults; bootloader and app startup may already have changed pin state.

## Active Board

Current active config is `V8_1` with `TWO_BUTTON`.

| Macro | GPIO | Role | Sleep or boot note |
| --- | ---: | --- | --- |
| `BUTTON_CIRCLE` | 1 | Wake button, UI button | RTC wake, input pull-up |
| `BUTTON_SQUARE` | 2 | Wake button, UI button | RTC wake, input pull-up |
| `PWR_CTRL` | 3 | Main switched power rail | Strapping pin, held low in deep sleep |
| `I2C_SCL` | 4 | I2C clock | Held low in deep sleep |
| `I2C_SDA` | 5 | I2C data | Held low in deep sleep |
| `BATTERY_PIN` | 6 | ADC placeholder for voltage helper | Not a real battery pin on V8.1 |
| `OLED_SDIN` | 7 | OLED SPI data | Held low in deep sleep |
| `USB_DET` | 8 | USB present detect | Input pull-up |
| `SCALE2_PDWN` | 9 | Secondary ADS1232 power-down | Held low in deep sleep |
| `BATTERY_CHARGING` | 10 | Charger status and wake source | RTC wake, input pull-up |
| `SCALE_DOUT` | 11 | Primary ADS1232 data | Input, held as input in deep sleep |
| `SCALE_SCLK` | 12 | Primary ADS1232 clock | Held low in deep sleep |
| `SCALE_PDWN` | 13 | Primary ADS1232 power-down | Held low in deep sleep |
| `ACC_PWR_CTRL` | 14 | Accessory power rail | Held low in deep sleep |
| `OLED_SCLK` | 15 | OLED SPI clock | Held low in deep sleep |
| `OLED_DC` | 16 | OLED data/command | Held low in deep sleep |
| `OLED_RST` | 17 | OLED reset | Held low in deep sleep |
| `OLED_CS` | 18 | OLED chip select | Held low in deep sleep |
| `SCALE2_DOUT` | 47 | Secondary ADS1232 data | Input, held as input in deep sleep |
| `SCALE2_SCLK` | 48 | Secondary ADS1232 clock | Held low in deep sleep |

## Modern Board Summary

| Board | Wake pins | Power pins | Primary scale | Secondary scale | OLED pins | Notable hazards |
| --- | --- | --- | --- | --- | --- | --- |
| `V8_1` | 1, 2, 10 | 3, 14 | 11, 12, 13 | 47, 48, 9 | 7, 15, 16, 17, 18 | `GPIO3` strapping |
| `V8_0` | 1, 2, 10 | 3, 14 | 11, 12, 13 | 47, 48, 9 | 7, 15, 16, 17, 18 | `GPIO3` strapping |
| `V7_5` | 1, 2, 10 | 3, 14 | 11, 12, 13 | 47, 48, 39 | 7, 15, 16, 17, 18 | `GPIO3` strapping |
| `V7_4` | 1, 2, 6 | 3, 13 | 8, 9, 18 | 47, 48, 35 | 10, 11, 12, 15, 16 | `GPIO3` strapping, `GPIO35` may be flash/PSRAM |
| `V7_3` | 1, 2, 6 | 3, 13 | 47, 48, 18 | 8, 9, missing `SCALE2_PDWN` | 10, 11, 12, 15, 16 | `GPIO3` strapping, legacy macro gap |
| `V7_2` | 1, 2, 6 | 3 | 8, 9, 13 | not defined | 10, 11, 12, 15, 16 | `GPIO3` strapping, legacy macro gap |

## Sleep-Hold Contract

`esp32_sleep()` in `include/power.h` must cover every pin connected to a device powered by the switched rails.

| Group | Deep-sleep state | Released in setup |
| --- | --- | --- |
| OLED pins | output low, `gpio_hold_en` | yes |
| Primary ADS1232 `SCLK` and `PDWN` | output low, `gpio_hold_en` | yes |
| Primary ADS1232 `DOUT` | input, `gpio_hold_en` | yes |
| Secondary ADS1232 pins | `SCLK`/`PDWN` output low, `DOUT` input, all held | yes |
| I2C pins | output low, `gpio_hold_en` | yes |
| `ACC_PWR_CTRL` | output low, `gpio_hold_en` | yes |
| `PWR_CTRL` | output low, `gpio_hold_en` | yes |
| `BUTTON_CIRCLE`, `BUTTON_SQUARE`, `BATTERY_CHARGING` | RTC input pull-up, EXT1 wake any-low | RTC mode released in setup |

## Change Checklist

- If a pin is used for wake, verify RTC GPIO support and update `PIN_BITMASK`.
- If a pin touches the switched power domain, update both the sleep hold and setup release paths.
- If a pin is a strapping pin, document the external pull/load before changing it.
- If a pin is `GPIO26-GPIO37`, verify the exact ESP32-S3 module flash/PSRAM wiring.
- If a pin is read with `digitalRead()` or `gpio_get_level()`, confirm input mode is set first.
- Do not move GPIO, I2C, SPI, power-rail, or OLED operations into AsyncTCP callbacks.
