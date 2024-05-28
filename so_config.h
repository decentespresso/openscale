#ifndef CONFIG_H
#define CONFIG_H

// BLE UUIDs

#define CUUID_DECENTSCALE_READ "0000FFF4-0000-1000-8000-00805F9B34FB"
#define CUUID_DECENTSCALE_WRITE "000036F5-0000-1000-8000-00805F9B34FB"
#define CUUID_DECENTSCALE_WRITEBACK "83CDC3D4-3BA2-13FC-CC5E-106C351A9352"
#define SUUID_DECENTSCALE "0000FFF0-0000-1000-8000-00805F9B34FB"

//#define ESPNOW

//#define SH1116
#define SSD1306
//#define OLD_PIN  //use old pins on hx711, buzzer. Notice: It'll cover FIVE_BUTTON pin

//#define ROTATION_180

#define TWO_BUTTON

//POWER DISPLAY
#define SHOWBATTERY
//#define CHECKBATTERY

//SCALE CONFIG
#define LINE1 (char*)"Version: 4.1"
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

//#define CAL //both button down during welcome text, start calibration
#define BT
//#define DEBUG
//#define DEBUG_BT
//#define DEBUG_BATTERY

//ADC BIT DEPTH
#define ADC_BIT 12

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

#ifdef ESP32
#define BATTERY_PIN 36
#define USB_PIN 35
#define BUTTON_SET 33  //33
#define BUTTON_TARE 32
#define GPIO_NUM_BUTTON_POWER GPIO_NUM_33


#define BUZZER 22
#define BUZZER_LED 27
#define I2C_SCL 19
#define I2C_SDA 23
#define HX711_SCL 18
#define HX711_SDA 5
#define OLED_RST 17

#endif
#endif