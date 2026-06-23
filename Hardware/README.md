# Decent Open Scale — Hardware Reference

> **Note:** The [original Bill of Materials](#original-bill-of-materials-archive) at the bottom of this document dates from an early hardware revision (circa V1.0.0 era). It is preserved for archival reference only and does **not** reflect the current PCB design. The sections below are generated from the actual firmware source code ([`include/config.h`](../include/config.h)) and represent the hardware as it exists today across PCB revisions V4 through V8.1.

---

## Active Configuration (PCB V8.1)

The firmware currently builds with `#define V8_1`. This is the reference hardware.

| # | Component | Part / Driver | Qty | Notes |
|---|-----------|---------------|-----|-------|
| 1 | **MCU** | ESP32 (ESP32-S3 target) | 1 | BLE + WiFi + USB Serial. PlatformIO build target is ESP32-S3. |
| 2 | **Load-cell ADC** | ADS1232 (24-bit) | 1 | Primary ADC. Two channels supported: SCALE (DOUT=11, SCLK=12, PDWN=13) and SCALE2 (DOUT=47, SCLK=48, PDWN=9). Uses custom [`ADS1232_ADC`](../include/ADS1232_ADC.h) library. |
| 3 | **Battery ADC** | ADS1115 (16-bit, I2C addr 0x48) | 1 | Reads battery voltage on channel A0 via I2C. Falls back to ESP32 internal ADC if not detected. |
| 4 | **Display** | SH1106 128×64 OLED | 1 | Driven via HW_SPI (MOSI=7, SCLK=15, DC=16, RST=17, CS=18). Uses U8g2 library. Also supports SH1116 and SSD1306 on other revisions. |
| 5 | **Accelerometer** | BMA400 (3-axis, I2C addr 0x14) | 1 | Orientation-aware power management (face-up/down auto-off). Used on V8.0. *V8.1 does not define an accelerometer; the gyro block is conditionally compiled out.* |
| 6 | **Touch buttons** | TTP223 capacitive touch | 2 | `BUTTON_CIRCLE` (GPIO 1) for power, `BUTTON_SQUARE` (GPIO 2). AceButton library with configurable debounce/long-press/double-click. Link point A for active-low output. |
| 7 | **Buzzer** | Active buzzer | 1 | GPIO 38 (on V8.0; V8.1 does not define `BUZZER`). |
| 8 | **Battery** | Li-Po 503450 (3.7 V) | 1 | With protection board, 2-wire. Charging status on GPIO 10. |
| 9 | **Voltage divider** | 100K + 100K (1/4 W) | 2 | For ESP32 internal ADC fallback battery reading. Older boards (V7.2) use 33K + 100K. |
| 10 | **Power MOSFET** | P-ch MOSFET (PWR_CTRL GPIO 3) | 1 | Gates main system power. Allows firmware-controlled shutdown. |
| 11 | **Accel power gate** | ACC_PWR_CTRL (GPIO 14) | 1 | Independent power rail for the accelerometer so it can be turned off in deep sleep. |
| 12 | **USB detection** | USB_DET (GPIO 8) | 1 | Digital input; high when USB is connected (V8.1+). |
| 13 | **USB-C connectors** | Female + Male USB-C | 1 each | Charging, firmware upload, and serial communication. |
| 14 | **Load cell** | i2000 or similar strain-gauge | 1 | Full-bridge. Bigger = more stable; thinner form factor preferred for enclosure fit. |

### V8.1 Pin Map

```
I2C SDA        5     OLED MOSI/SDIN   7     SCALE_DOUT     11
I2C SCL        4     OLED SCLK        15     SCALE_SCLK     12
BATTERY_PIN    6     OLED DC          16     SCALE_PDWN     13
USB_DET        8     OLED RST         17     SCALE2_DOUT    47
PWR_CTRL       3     OLED CS          18     SCALE2_SCLK    48
BATTERY_CHARG  10    BUTTON_CIRCLE    1      SCALE2_PDWN    9
ACC_PWR_CTRL   14    BUTTON_SQUARE    2
```

---

## Component Variants Across PCB Revisions

The codebase supports multiple hardware choices selected at compile time via `#define` flags in [`config.h`](../include/config.h).

### ADC (Load Cell)

| Define | Chip | Used On |
|--------|------|---------|
| `ADS1232ADC` | ADS1232 24-bit (SPI-like: DOUT/SCLK/PDWN) | V5–V8.1 (primary) |
| `HX711ADC` | HX711 24-bit | V3–V4, and as fallback pin alias on all newer boards |

### ADC (Battery Monitoring)

| Define | Chip | Used On |
|--------|------|---------|
| `ADS1115ADC` | ADS1115 16-bit I2C | V7.5, V8.0, V8.1 |
| *(none)* | ESP32 internal ADC + voltage divider | V4–V7.4 (fallback on all boards) |

### Display Controller

| Define | Driver | Interface Options |
|--------|--------|-------------------|
| `SH1106` | SH1106 128×64 | HW_SPI / HW_I2C / SW_SPI / SW_I2C |
| `SH1116` | SH1116 128×64 | HW_SPI / HW_I2C / SW_SPI / SW_I2C |
| `SSD1306` | SSD1306 128×64 | HW_SPI / HW_I2C / SW_SPI / SW_I2C |

All driven by the **U8g2** library. The V8.1 active config is `SH1106` over `HW_SPI`.

### Accelerometer / Gyroscope

| Define | Chip | Interface | Used On |
|--------|------|-----------|---------|
| `ACC_MPU6050` | MPU6050 6-axis (Adafruit_MPU6050) | I2C | V7.2 (and earlier) |
| `ACC_BMA400` | BMA400 3-axis (SparkFun BMA400) | I2C addr 0x14 / 0x15 | V8.0 |
| *(none)* | No accelerometer | — | V8.1 (current) |

The accelerometer is used for orientation-aware behaviour:
- Face-down detection → auto power-off.
- Face-up / motion detection → prevent accidental power-on.

A dedicated power gate (`ACC_PWR_CTRL`) lets the firmware cut power to the accelerometer during deep sleep.

### Button Configuration

| Define | Count | Notes |
|--------|-------|-------|
| `TWO_BUTTON` | 2 | `BUTTON_CIRCLE` + `BUTTON_SQUARE`. Current default. |
| `FOUR_BUTTON` | 4 | Supported but not active. |
| *(legacy)* | 2 | V3 used `BUTTON_SET` + `BUTTON_TARE`. |

All buttons are TTP223 capacitive touch modules with the AceButton library providing debounce, long-press, and double-click detection.

### Connectivity

| Feature | Define | Notes |
|---------|--------|-------|
| BLE | `BT` | Decent Scale BLE protocol (service `fff0`, read `fff4`, write `36f5`). Model byte `0x03`. |
| WiFi / OTA | `WIFIOTA` | Web server, WebSocket, over-the-air firmware updates. |
| USB Serial | *(always on)* | Weight data output and debug console. |
| ESP-NOW | `ESPNOW` | Optional peer-to-peer ESP communication. Currently commented out. |

---

## PCB Revision History

| Revision | Display | ADC | Accel | Battery ADC | Key Changes |
|----------|---------|-----|-------|-------------|-------------|
| **V8.1** | SH1106 HW_SPI | ADS1232 | *(none)* | ADS1115 | Removed accelerometer; dual-channel scale support (SCALE2). |
| **V8.0** | SH1106 HW_SPI | ADS1232 | BMA400 | ADS1115 | Switched to BMA400; added `ACC_INT` on GPIO 21. |
| **V7.5** | SH1106 HW_SPI | ADS1232 | *(commented out)* | *(none)* | Pin shuffle; `SCALE2_PDWN` moved to GPIO 39. |
| **V7.4** | SH1106 HW_SPI | ADS1232 | *(none)* | *(none)* | Major pin re-map; DOUT=8, SCLK=9. |
| **V7.3** | SH1106 HW_SPI | ADS1232 | *(none)* | *(none)* | `SCALE2_DOUT` introduced on GPIO 8. |
| **V7.2** | SH1106 HW_SPI | ADS1232 | MPU6050 | *(none)* | Last revision with MPU6050; 33K+100K battery divider. |
| **V6** | SH1106 HW_SPI | ADS1232 | *(none)* | *(none)* | Simplified pinout. |
| **V5** | SH1116 SW_SPI | ADS1232 | *(none)* | *(none)* | First ADS1232 revision; SW_SPI display. |
| **V4** | OLED HW_SPI | HX711 | *(none)* | *(none)* | HX711-based; ESP32 V1.0.0 era. |
| **V3** | OLED HW_SPI | HX711 | *(none)* | *(none)* | Earliest supported revision. |

---

## Library Dependencies (Hardware-Related)

| Library | Purpose |
|---------|---------|
| **U8g2** (`U8g2lib.h`) | OLED display driver (SH1106/SH1116/SSD1306) |
| **ADS1232_ADC** (custom) | 24-bit ADC for load cell readout |
| **Adafruit_ADS1X15** | ADS1115 16-bit ADC for battery monitoring |
| **Adafruit_MPU6050** | MPU6050 6-axis accelerometer (V7.2 and earlier) |
| **SparkFun BMA400** | BMA400 3-axis accelerometer (V8.0) |
| **AceButton** | Button debounce, long-press, double-click handling |
| **BLEDevice / BLEServer** (ESP32 BLE stack) | Bluetooth Low Energy communication |

---

## Original Bill of Materials (Archive)

*The table below is the original BOM from the early hardware prototype (ESP32 V1.0.0 era). It is kept here for historical reference. For current hardware, see the [V8.1 configuration](#active-configuration-pcb-v81) above.*

| Part Number | Part Name    | Description               | Quantity | Vendor/Source                                                                                                                         | Notes                                                                                                                  |
|-------------|--------------|---------------------------|----------|----------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------|
| 1           | ESP32 V1.0.0 | Main control unit         | 1        | [Taobao](https://item.taobao.com/item.htm?_u=pnmg8f7c3d&id=628078369041&spm=a1z09.2.0.0.10072e8dCAfwWX)                               | This particular one has battery support, which I assume it has a TP4054 on it. Two versions are available: MicroUSB and Type-C. |
| 2           | Buzzer 9650  | Make sound                | 1        | [Taobao](https://item.taobao.com/item.htm?_u=pnmg8fd3d7&id=634342562820&spm=a1z09.2.0.0.10072e8dCAfwWX)                               | Use a powered one for ease of use, but a powerless one can change the tone.                                           |
| 3           | SSD1306 SH1116| OLED display              | 1        | [Taobao](https://item.taobao.com/item.htm?_u=7nmg8f9838&id=677128362825&spm=a1z09.2.0.0.43f22e8dkkGbTz&skuId=5426714191529)          | SH1116 is a bit cheaper, should add a line: #define SH1116.                                                                          |
| 4           | MPU6050      | 6-axle sensor             | 1        | [Taobao](https://item.taobao.com/item.htm?_u=pnmg8f529e&id=609979451344&spm=a1z09.2.0.0.10072e8dCAfwWX)                               | For side and bottom up power off, and avoid accidental power on.                                                       |
| 5           | HX711        | ADC                       | 1        | [Taobao](https://item.taobao.com/item.htm?_u=pnmg8f6256&id=611734839533&spm=a1z09.2.0.0.10072e8dCAfwWX)                               | The red one has a metal shield, I don't know if it's good, I just didn't see the difference.                           |
| 6           | Loadcell     | i2000                     | 1        | [1688](https://detail.1688.com/offer/671859449208.html?spm=a26352.13672862.offerlist.60.318a40e0k6dDDa)                               | Bigger ones are more stable, but we have to choose the thinner ones.                                                   |
| 7           | Female USB-c | Charging and firmware update | 1     | [Taobao](https://item.taobao.com/item.htm?_u=pnmg8fa987&id=721229692231&spm=a1z09.2.0.0.10072e8dCAfwWX)                               |                                                                                                                        |
| 8           | Male USB-c   | Connect to MCU            | 1        | [Taobao](https://item.taobao.com/item.htm?_u=pnmg8fe973&id=609433305908&spm=a1z09.2.0.0.10072e8dCAfwWX)                               |                                                                                                                        |
| 9           | 503450 Battery|                           | 1        | [1688](https://detail.1688.com/offer/741144487402.html?spm=a360q.8274423.0.0.7b854c9ayJtmcd)                                            | With protect board and 2 wires out.                                                                                    |
| 10          | 100K resistor| 1/4w                      | 2        | [Tmall](https://detail.tmall.com/item.htm?_u=pnmg8fed34&id=13302997879&spm=a1z09.2.0.0.10072e8dCAfwWX&skuId=3756188445710)              | Voltage divider for ESP32 3.3v max ADC reading the battery level.                                                      |
| 11          | TTP223       | Touch button              | 2        | [Taobao](https://item.taobao.com/item.htm?_u=pnmg8f71e5&id=611366998227&spm=a1z09.2.0.0.10072e8dCAfwWX)                               | Link point A to have logic low output.                                                                                 |

### Original Wiring Diagram
![Wiring](https://github.com/decentespresso/openscale/assets/11464550/76d51924-c86f-4896-b179-38d1a397d9e2)
