#ifndef CONFIG_H
#define CONFIG_H

// BLE UUIDs

// #define SERVICE_UUID "0000FFF0-0000-1000-8000-00805F9B34FB"
// #define CHARACTERISTIC_UUID_RX "0000FFF4-0000-1000-8000-00805F9B34FB"
// #define CHARACTERISTIC_UUID_TX "0000FFF5-0000-1000-8000-00805F9B34FB"
// #define CHARACTERISTIC_UUID_TARE "000036F6-0000-1000-8000-00805F9B34FB"

#define CUUID_DECENTSCALE_READ "0000FFF4-0000-1000-8000-00805F9B34FB"
#define CUUID_DECENTSCALE_WRITE "000036F5-0000-1000-8000-00805F9B34FB"
#define CUUID_DECENTSCALE_WRITEBACK "83CDC3D4-3BA2-13FC-CC5E-106C351A9352"
#define SUUID_DECENTSCALE "0000FFF0-0000-1000-8000-00805F9B34FB"

/*
#define SERVICE_UUID            "000036F0-0000-1000-8000-00805F9B34FB"
#define CHARACTERISTIC_UUID_RX  "000036F4-0000-1000-8000-00805F9B34FB"
#define CHARACTERISTIC_UUID_TX  "000036F5-0000-1000-8000-00805F9B34FB"
*/
//#define ESP32C3
#define SH1116
//#define SSD1306
//#define OLD_PIN  //use old pins on hx711, buzzer. Notice: It'll cover FIVE_BUTTON pin
#define ROTATION_180

#define TWO_BUTTON  //TWO_BUTTON THREE_BUTTON
//#define FOUR_BUTTON
//#define FIVE_BUTTON

//POWER DISPLAY
//#define CHECKBATTERY

//SCALE CONFIG
#define LINE1 (char*)"Version: 4.0"
#define LINE2 (char*)"Built-date(YYYYMMDD): 20240407"
#define LINE3 (char*)"S/N: DRS069"  //序列号 069
#define VERSION /*版本号 version*/ LINE1, /*编译日期*/ LINE2, /*序列号*/ LINE3
//#define WELCOME1 (char*)"Lian"
#define WELCOME1 (char*)"Half Decent"
#define WELCOME2 (char*)"w2"
#define WELCOME3 (char*)"w3"
#define WELCOME WELCOME1, FONT_EXTRACTION

//Language
//#define ENGLISH
#define CHINESE
#ifdef ENGLISH
#define TEXT_ESPRESSO "Espresso"
#define TEXT_ESPRESSO_AUTO_TARE_CONTAINER "Espresso *"
#define TEXT_ESPRESSO_MANUAL_TARE_CONTAINER "Espresso *"
#define TEXT_POUROVER "Pour Over"
#define TEXT_POUROVER_AUTO_TARE_CONTAINER "Pour Over *"
#define TEXT_POUROVER_MANUAL_TARE_CONTAINER "Pour Over *"
#endif
#ifdef CHINESE
#define TEXT_ESPRESSO "意式模式"
#define TEXT_ESPRESSO_AUTO_TARE_CONTAINER "意式模式 *"
#define TEXT_ESPRESSO_MANUAL_TARE_CONTAINER "意式模式 *"
#define TEXT_POUROVER "手冲模式"
#define TEXT_POUROVER_AUTO_TARE_CONTAINER "手冲模式 *"
#define TEXT_POUROVER_MANUAL_TARE_CONTAINER "手冲模式 *"


#endif


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

//BATTERY
#define BATTERY_VOLTAGE_MIN 3.3

#define BT
//#define DEBUG
//#define DEBUG_BT
//#define DEBUG_BATTERY

//ADC BIT DEPTH
#if defined(ESP8266) || defined(AVR)
#define ADC_BIT 10
#endif
#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
#define ADC_BIT 12
#endif

//BUTTON

#define ACEBUTTON  //ACEBUTTON ACEBUTTONT
#define DEBOUNCE 50
#define LONGCLICK 1000
#define DOUBLECLICK 800
#define BUTTON_KEY_DELAY 150

//DISPLAY
#define HW_I2C        //HW_I2C  HW_SPI  SW_I2C  SW_SPI   //oled连接方式
#define Margin_Top 0  //显示边框
#define Margin_Bottom 0
#define Margin_Left 0
#define Margin_Right 0

//GYRO
#define GYROFACEUP
//#define GYROFACEDOWN //印刷面朝下

//PIN 针脚定义
#ifdef AVR
#ifdef HW_SPI
#define OLED_SCL 6
#define OLED_SDA 7
#define OLED_RST 8
#define OLED_DC 9
#define OLED_CS 10
#endif
#define BUTTON_SET 2
#define BUTTON_PLUS 3
#define BUTTON_MINUS 4
#define BUTTON_TARE 5
#define BUTTON_KEY 6
#define HX711_SDA A2
#define HX711_SCL A3
#define I2C_SDA A4
#define I2C_SCL A5
#define BUZZER 12

#ifdef CHECKBATTERY
#define BATTERY_LEVEL A0
#define USB_LEVEL A1
#endif

#endif  //AVR

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
#define BUTTON_SET 10
#define BUTTON_PLUS 11
#define BUTTON_MINUS 12
#define BUTTON_TARE 13
#define BUTTON_KEY 14
#define BUTTON_DEBUG 24
#define HX711_SDA 16
#define HX711_SCL 17
#define I2C_SDA 4
#define I2C_SCL 5
#define OLED_CS 6   //low
#define OLED_DC 7   //low
#define OLED_RST 8  //high
#define BUZZER 23   //15

#ifdef CHECKBATTERY
#define BATTERY_LEVEL 26
#define USB_LEVEL 27
#endif  //CHECKBATTERY

#endif  //defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)

#ifndef ESP32C3
#ifdef ESP32
//#define THERMISTOR_PIN 39
#define BATTERY_PIN 36
#define BUTTON_SET 33  //33
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
#define BUTTON_PLUS 26   //not in use
#define BUTTON_MINUS 25  //not in use
#endif
#define BUTTON_TARE 32
#define BUTTON_KEY 4     //not in use
#define BUTTON_DEBUG 13  //not in use
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_33


#define BUZZER 22
#define BUZZER_LED 27
#define I2C_SCL 19
#define I2C_SDA 23
#define HX711_SCL 18
#define HX711_SDA 5
#define OLED_RST 17

#ifdef CHECKBATTERY
#define BATTERY_LEVEL 25
#define USB_LEVEL 26
#endif  //CHECKBATTERY
#endif  //ESP32 Pin map
#endif  //not ESP32C3

#ifdef ESP32C3
#define BATTERY_PIN 0
#define BUTTON_SET 2
#define BUTTON_TARE 3
#define GPIO_NUM_BUTTON_POWER BUTTON_SET

#define BUZZER 10
#define I2C_SCL 6
#define I2C_SDA 7
#define HX711_SCL 11
#define HX711_SDA 5
#define OLED_RST 4
#endif  //ESP32C3 Pin map

#endif