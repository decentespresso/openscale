#ifndef CONFIG_H
#define CONFIG_H

// BLE UUIDs

#define CUUID_DECENTSCALE_READ "fff4"
#define CUUID_DECENTSCALE_WRITE "36f5"
// #define CUUID_DECENTSCALE_WRITEBACK "83CDC3D4-3BA2-13FC-CC5E-106C351A9352"
#define SUUID_DECENTSCALE "fff0"

//#define ESPNOW

// #define SW_SPI //HW_I2C  HW_SPI  SW_I2C  SW_SPI   //oled linkage
// #define SH1106
#define V8_1
#define WIFIOTA
//#define CAL

//#define SSD1306
//#define OLD_PIN  //use old pins on hx711, buzzer. Notice: It'll cover FIVE_BUTTON pin

#define TWO_BUTTON

//POWER DISPLAY
#define SHOWBATTERY
//#define CHECKBATTERY

//SCALE CONFIG
#define LINE1 (char*)"FW: 3.0.0beta1"
#define LINE2 (char*)"Built-date(YYYYMMDD): 20250809"
#define LINE3 (char*)"S/N: HDS001"  //Serial number
#define VERSION /*version*/ LINE1, /*compile date*/ LINE2, /*sn*/ LINE3
//About info
#define FIRMWARE_VER LINE1
//#define WELCOME1 (char*)"Lian"
#define WELCOME1 (char*)"Half Decent"
#define WELCOME2 (char*)"w2"
#define WELCOME3 (char*)"w3"
#define WELCOME WELCOME1, FONT_EXTRACTION
#define BUZZER_DURATION 5

#define PositiveTolerance 25  // positive tolerance range in grams
#define NegativeTolerance 5   // negative tolerance range in grams
#define OledTolerance 0.09

//ntc
//#define THERMISTOR_PIN 39
#define SERIESRESISTOR 10000
#define NOMINAL_RESISTANCE 10000
#define NOMINAL_TEMPERATURE 25
#define BCOEFFICIENT 3950
#define FILTER_CONSTANT 0.1

//#define CAL //both button down during welcome text, start calibration
#define BT

//ADC BIT DEPTH
#define ADC_BIT 12

//BUTTON

#define ACEBUTTON  //ACEBUTTON ACEBUTTONT
#define DEBOUNCE 200
#define LONGCLICK 1500
#define DOUBLECLICK 800
#define CLICK 400
#define BUTTON_KEY_DELAY 150

//DISPLAY
#define Margin_Top 0  //显示边框
#define Margin_Bottom 0
#define Margin_Left 0
#define Margin_Right 0

//ESP32S3

#ifdef ESP32


#ifdef V8_1
#define PCB_VER (char*)"PCB: 8.1"
#define HW_SPI
#define SH1106
#define ADS1232ADC
#define ADS1115ADC
#define ROTATION_180

#define I2C_SCL 4
#define I2C_SDA 5
#define BATTERY_PIN 6 //wasn't used but to keep getVoltage(battery_pin) working. Any number is good for that.
#define OLED_SDIN 7
#define OLED_SCLK 15
#define OLED_DC 16
#define OLED_RST 17
#define OLED_CS 18
#define USB_DET 8
#define PWR_CTRL 3
//#define NTC 9
#define BATTERY_CHARGING 10
#define SCALE_DOUT 11
#define SCALE_SCLK 12
#define SCALE_PDWN 13
#define SCALE2_DOUT 47
#define SCALE2_SCLK 48
#define SCALE2_PDWN 9
#define ACC_PWR_CTRL 14
#define BUTTON_CIRCLE 1  //33
#define BUTTON_SQUARE 2
#if defined(TWO_BUTTON) || defined(FOUR_BUTTON)
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_1
#endif

#define SCALE_A0 -1
#define HX711_SCL 12
#define HX711_SDA 11
#endif


#ifdef V8_0
#define PCB_VER (char*)"PCB: 8.0.0"
#define HW_SPI
#define SH1106
#define ADS1232ADC
#define ADS1115ADC
#define ACC_BMA400
#define ROTATION_180
#define GYROFACEDOWN  //GYRO //#define GYROFACEUP

#define I2C_SCL 4
#define I2C_SDA 5
#define BATTERY_PIN 6 //wasn't used but to keep getVoltage(battery_pin) working. Any number is good for that.
#define OLED_SDIN 7
#define OLED_SCLK 15
#define OLED_DC 16
#define OLED_RST 17
#define OLED_CS 18
#define USB_DET 8
#define PWR_CTRL 3
//#define NTC 9
#define BATTERY_CHARGING 10
#define SCALE_DOUT 11
#define SCALE_SCLK 12
#define SCALE_PDWN 13
#define ACC_PWR_CTRL 14
#define ACC_INT 21
#define SCALE2_DOUT 47
#define SCALE2_SCLK 48
#define SCALE2_PDWN 9
#define BUTTON_CIRCLE 1  //33
#define BUTTON_SQUARE 2
#if defined(TWO_BUTTON) || defined(FOUR_BUTTON)
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_1
#endif
#define BUZZER 38

#define SCALE_A0 -1
#define HX711_SCL 12
#define HX711_SDA 11
#endif


#ifdef V7_5
#define PCB_VER (char*)"PCB: 7.5"
#define HW_SPI
#define SH1106
#define ADS1232ADC
//#define ACC_MPU6050
#define ROTATION_180
#define GYROFACEDOWN  //GYRO //#define GYROFACEUP

#define I2C_SCL 4
#define I2C_SDA 5
#define BATTERY_PIN 6
#define OLED_SDIN 7
#define OLED_SCLK 15
#define OLED_DC 16
#define OLED_RST 17
#define OLED_CS 18
#define USB_DET 8
#define PWR_CTRL 3
#define NTC 9
#define BATTERY_CHARGING 10
#define SCALE_DOUT 11
#define SCALE_SCLK 12
#define SCALE_PDWN 13
#define ACC_PWR_CTRL 14
#define ACC_INT 21
#define SCALE2_DOUT 47
#define SCALE2_SCLK 48
#define SCALE2_PDWN 39
#define BUTTON_CIRCLE 1  //33
#define BUTTON_SQUARE 2
#if defined(TWO_BUTTON) || defined(FOUR_BUTTON)
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_1
#endif
//#define BUZZER 38

#define SCALE_A0 -1
#define HX711_SCL 12
#define HX711_SDA 11
#endif


#ifdef V7_4
#define HW_SPI
#define SH1106
#define ADS1232ADC
#define ROTATION_180
#define GYROFACEDOWN  //GYRO //#define GYROFACEUP

#define I2C_SCL 4
#define I2C_SDA 5
#define BATTERY_CHARGING 6
#define BATTERY_PIN 7
#define OLED_DC 15
#define OLED_RST 16
#define USB_DET 17
#define SCALE_PDWN 18
#define SCALE_DOUT 8
#define PWR_CTRL 3
#define SCALE_SCLK 9
#define OLED_CS 10
#define OLED_SDIN 11
#define OLED_SCLK 12
#define ACC_PWR_CTRL 13
#define NTC 14
#define ACC_INT 21
#define SCALE2_DOUT 47
#define SCALE2_SCLK 48
#define SCALE2_PDWN 35
#define BUTTON_CIRCLE 1  //33
#define BUTTON_SQUARE 2
#if defined(TWO_BUTTON) || defined(FOUR_BUTTON)
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_1
#endif
#define BUZZER 38

#define SCALE_A0 -1
#define HX711_SCL 9
#define HX711_SDA 8
#endif


#ifdef V7_3
#define HW_SPI
#define SH1106
#define ADS1232ADC
#define ROTATION_180
#define GYROFACEDOWN  //GYRO //#define GYROFACEUP

#define I2C_SCL 4
#define I2C_SDA 5
#define BATTERY_CHARGING 6
#define BATTERY_PIN 7
#define OLED_DC 15
#define OLED_RST 16
#define SCALE_PDWN 18
#define SCALE2_DOUT 8
#define PWR_CTRL 3
#define SCALE2_SCLK 9
#define OLED_CS 10
#define OLED_SDIN 11
#define OLED_SCLK 12
#define ACC_PWR_CTRL 13
#define NTC 14
#define MPU_INT 21
#define SCALE_DOUT 47
#define SCALE_SCLK 48
#define BUTTON_CIRCLE 1  //33
#define BUTTON_SQUARE 2
#if defined(TWO_BUTTON) || defined(FOUR_BUTTON)
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_1
#endif
#define BUZZER 38

#define SCALE_A0 -1
#define BUZZER_LED 38
#define HX711_SCL 9
#define HX711_SDA 8
#endif

#ifdef V7_2
#define PCB_VER (char*)"PCB: 7.2"
#define HW_SPI
#define SH1106
#define ACC_MPU6050
#define ADS1232ADC
#define ROTATION_180
#define GYROFACEDOWN  //GYRO //#define GYROFACEUP
#define PWR_CTRL 3
#define OLED_CS 10
#define OLED_DC 15
#define OLED_RST 16
#define OLED_SDIN 11
#define OLED_SCLK 12
#define SCALE_DOUT 8
#define SCALE_SCLK 9
#define SCALE_PDWN 13
#define SCALE_A0 -1
#define I2C_SCL 4
#define I2C_SDA 5
#define ROTATION_180

#define BATTERY_PIN 7
#define BATTERY_CHARGING 6
#define BUTTON_CIRCLE 1  //33
#define BUTTON_SQUARE 2
#if defined(TWO_BUTTON) || defined(FOUR_BUTTON)
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_1
#endif

#define BUZZER 38
#define BUZZER_LED 38
#define HX711_SCL 9
#define HX711_SDA 8
#endif

#ifdef V6
#define HW_SPI
#define SH1106
#define ADS1232ADC
#define ROTATION_180
#define GYROFACEDOWN  //GYRO //#define GYROFACEUP
#define OLED_CS 10
#define OLED_DC 15
#define OLED_RST 16
#define OLED_SDIN 13
#define OLED_SCLK 12
#define SCALE_DOUT 8
#define SCALE_SCLK 9
#define SCALE_PDWN 11
#define SCALE_A0 -1
#define I2C_SCL 4
#define I2C_SDA 5
#define ROTATION_180

#define BATTERY_PIN 7
#define BUTTON_CIRCLE 1  //33
#define BUTTON_SQUARE 2
#if defined(TWO_BUTTON) || defined(FOUR_BUTTON)
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_2
#endif

#define BUZZER 38
#define BUZZER_LED 38
#define HX711_SCL 9
#define HX711_SDA 8
#endif

#ifdef V5
#define SW_SPI
#define SH1116
#define ADS1232ADC
#define ROTATION_180
#define GYROFACEDOWN
#define OLED_CS 8
#define OLED_DC 15
#define OLED_RST 16
#define OLED_SDIN 6
#define OLED_SCLK 7
#define SCALE_DOUT 9
#define SCALE_SCLK 10
#define SCALE_PDWN 11
#define SCALE_A0 -1
#define I2C_SCL 4
#define I2C_SDA 5
#define ROTATION_180

#define BATTERY_PIN 3
#define USB_PIN 12
#define BUTTON_SQUARE 1  //33
#define BUTTON_CIRCLE 2
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_2


#define BUZZER 13
#define BUZZER_LED 13
#define HX711_SCL 18
#define HX711_SDA 5

#endif

#ifdef V4
//v4 pin
#define BATTERY_PIN 14
//#define USB_PIN 35
#define PIN_CHRG 48
#define PIN_STDBY 47
#define BUTTON_CIRCLE 2  //(v3.0 39, changed to 13 for rtc)33
#define BUTTON_SQUARE 1
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_2


#define BUZZER 38
#define BUZZER_LED 38  //set to different for only led indicating
#define I2C_SCL 4
#define I2C_SDA 5
#define OLED_CS 10
#define OLED_DC 21
#define OLED_RST 13
#define OLED_SDIN 11
#define OLED_SCLK 12
#define SCALE_DOUT 6
#define SCALE_SCLK 7
#define SCALE_PDWN 15
#define SCALE_TEMP 16
#define SCALE_A0 8
#define HX711_SCL 3
#define HX711_SDA 9
#endif

#ifdef V3
#define BATTERY_PIN 1
//#define USB_PIN 35
#define PIN_CHRG 42
#define PIN_STDBY 41
#define BUTTON_SET 2  //(v3.0 39, changed to 13 for rtc)33
#define BUTTON_TARE 38
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_2


#define BUZZER 15
#define BUZZER_LED 15  //set to different for only led indicating
#define I2C_SCL 9
#define I2C_SDA 8
#define OLED_CS 10
#define OLED_DC 21
#define OLED_RST 14
// #define HX711_SCL 18
// #define HX711_SDA 5
#define SCALE_DOUT 45
#define SCALE_SCLK 48
#define SCALE_PDWN 47
#define HX711_SCL SCALE_SCLK
#define HX711_SDA SCALE_DOUT
//#define OLED_RST 17

#endif

#endif
#endif
