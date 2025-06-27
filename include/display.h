#ifndef DISPLAY_H
#define DISPLAY_H
#include "config.h"
#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
#if defined(ESP8266) || defined(ESP32) || defined(AVR) || defined(ARDUINO_ARCH_RP2040)
#ifdef HW_I2C
#ifdef SSD1306
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/OLED_RST);
#endif
#ifdef SSD1312
U8G2_SSD1312_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/OLED_RST);
#endif
#if defined(SH1106) || defined(SH1116)
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/OLED_RST);
#endif
#endif
#ifdef HW_SPI
#ifdef SSD1306
U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/OLED_CS, /* dc=*/OLED_DC, /* reset=*/OLED_RST);
#endif
#if defined(SH1106) || defined(SH1116)
U8G2_SH1106_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/OLED_CS, /* dc=*/OLED_DC, /* reset=*/OLED_RST);
#endif
#endif
#ifdef SW_SPI
#if defined(SH1106) || defined(SH1116)
U8G2_SH1106_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/OLED_SCLK, /* sata=*/OLED_SDIN, /* cs=*/OLED_CS, /* dc=*/OLED_DC, /* reset=*/OLED_RST);
#endif
#endif
#ifdef SW_SPI_OLD
U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/13, /* data=*/11, /* cs=*/10, /* dc=*/9, /* reset=*/8);
#endif
#ifdef SW_SPI
U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/OLED_SCLK, /* data=*/OLED_SDIN, /* cs=*/OLED_CS, /* dc=*/OLED_DC, /* reset=*/OLED_RST);
#endif
#ifdef SW_I2C
U8G2_SSD1306_128X64_NONAME_1_SW_I2C u8g2(U8G2_R0, /* clock=*/OLED_SCL, /* data=*/OLED_SDA, /* reset=*/U8X8_PIN_NONE);
#endif
#endif
//显示屏初始化 https://github.com/olikraus/u8g2/wiki/u8g2reference
//设置字体 https://github.com/olikraus/u8g2/wiki/fntlistall
//中文字体 https://github.com/larryli/u8g2_wqy
//#include "u8g2_soso.h"
#define FONT_L u8g2_font_logisoso24_tf
#define FONT_M u8g2_font_fub14_tr    //u8g2_font_wqy16_t_gb2312 u8g2_font_fub14_tr soso16
#define FONT_S u8g2_font_helvR10_tr  //u8g2_font_wqy14_t_gb2312 u8g2_font_helvR12_tr soso14
#define FONT_TIMER u8g2_font_luBS18_tr
#define FONT_GRAM u8g2_font_luBS18_tr
#define FONT_WEIGHT u8g2_font_logisoso32_tf
#define OLED_BUTTON_WINDOW_WIDTH 10
#define OLED_BLE_WINDOW_HEIGHT 3
//#define FONT_XS u8g2_font_wqy12_t_gb2312
#define FONT_EXTRACTION u8g2_font_fub14_tr
//#define FONT_S FONT_M
#define FONT_BATTERY u8g2_font_battery19_tn
//macros
//文本对齐 AC居中 AR右对齐 AL左对齐 T为要显示的文本
#define LCDWidth u8g2.getDisplayWidth()
#define LCDHeight u8g2.getDisplayHeight()
#define AC(T) ((LCDWidth - u8g2.getUTF8Width(T)) / 2 - Margin_Left - Margin_Right)
#define AR(T) (LCDWidth - u8g2.getUTF8Width(T) - Margin_Right)
#define AL(T) (u8g2.getUTF8Width(T) + Margin_Left)
#define AM() ((LCDHeight + u8g2.getMaxCharHeight()) / 2 - Margin_Top - Margin_Bottom)
#define AB() (LCDHeight - u8g2.getMaxCharHeight() - Margin_Bottom)
#define AT() (u8g2.getMaxCharHeight() + Margin_Top)

char minsec[10] = "00:00";  //时钟显示样式00:00
// void refreshOLED(char* input);
// void refreshOLED(char* input, const uint8_t* font);
// void refreshOLED(char* input1, char* input2);
// void refreshOLED(char* input1, char* input2, const uint8_t* font);
// void refreshOLED(int input0, char* input1, char* input2);
// void refreshOLED(char* input1, char* input2, char* input3);
//icon tool https://lopaka.app/sandbox
static const unsigned char image_circle[] = { 0xe0, 0x03, 0x38, 0x0e, 0x0c, 0x18, 0x06, 0x30, 0x02, 0x20, 0x03, 0x60, 0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x03, 0x60, 0x02, 0x20, 0x06, 0x30, 0x0c, 0x18, 0x38, 0x0e, 0xe0, 0x03, 0x00, 0x00 };
static const unsigned char image_square[] = { 0xfe, 0x1f, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0xfe, 0x1f, 0x00, 0x00 };
static const unsigned char image_ble_enabled[] = { 0x03, 0x05, 0x09, 0x11, 0x09, 0x05, 0x03, 0x05, 0x09, 0x11, 0x09, 0x05, 0x03 };
static const unsigned char image_ble_disabled[] = { 0x18, 0x00, 0x28, 0x00, 0x49, 0x00, 0x8a, 0x00, 0x4c, 0x00, 0x28, 0x00, 0x18, 0x00, 0x28, 0x00, 0x48, 0x00, 0x88, 0x00, 0x48, 0x01, 0x28, 0x02, 0x18, 0x00 };
static const unsigned char image_battery_0[] = { 0x08, 0x36, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x3e };
static const unsigned char image_battery_1[] = { 0x08, 0x36, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x5d, 0x41, 0x3e };
static const unsigned char image_battery_2[] = { 0x08, 0x36, 0x41, 0x41, 0x41, 0x41, 0x41, 0x5d, 0x41, 0x5d, 0x41, 0x3e };
static const unsigned char image_battery_3[] = { 0x08, 0x36, 0x41, 0x41, 0x41, 0x5d, 0x41, 0x5d, 0x41, 0x5d, 0x41, 0x3e };
static const unsigned char image_battery_4[] = { 0x08, 0x36, 0x41, 0x5d, 0x41, 0x5d, 0x41, 0x5d, 0x41, 0x5d, 0x41, 0x3e };
static const unsigned char image_battery_charging[] = { 0x08, 0x36, 0x41, 0x51, 0x59, 0x7d, 0x7f, 0x5f, 0x4d, 0x45, 0x41, 0x3e };
char* sec2minsec(unsigned long n);
char* sec2sec(unsigned long n);
char* ltrim(char* s);
char* rtrim(char* s);
char* trim(char* s);


void refreshOLED(char* input) {
  u8g2.firstPage();
  u8g2.setFont(FONT_M);
  do {
    //1行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawUTF8(AC(input), AM() - 5, input);
  } while (u8g2.nextPage());
}

void refreshOLED(char* input, const uint8_t* font) {
  u8g2.firstPage();
  u8g2.setFont(font);
  do {
    //1行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawUTF8(AC(input), AM() - 5, input);
  } while (u8g2.nextPage());
}

void refreshOLED(char* input1, char* input2) {
  u8g2.firstPage();
  u8g2.setFont(FONT_M);
  do {
    //2行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawUTF8(AC(input1), u8g2.getMaxCharHeight(), input1);
    u8g2.drawUTF8(AC(input2), LCDHeight - 5, input2);
  } while (u8g2.nextPage());
}

void refreshOLED(char* input1, char* input2, const uint8_t* font) {
  u8g2.firstPage();
  u8g2.setFont(font);
  do {
    //2行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawUTF8(AC(input1), u8g2.getMaxCharHeight(), input1);
    u8g2.drawUTF8(AC(input2), LCDHeight - 5, input2);
  } while (u8g2.nextPage());
}

void refreshOLED(int input0, char* input1, char* input2) {
  u8g2.firstPage();
  u8g2.setFont(FONT_M);
  do {
    //3行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawUTF8(AC(input1), u8g2.getMaxCharHeight() + 5, input1);
    u8g2.drawUTF8(AC(input2), LCDHeight - 10, input2);
    u8g2.drawLine(94, 40, 90, 46);
  } while (u8g2.nextPage());
}

void refreshOLED(char* input1, char* input2, char* input3) {
  u8g2.firstPage();
  u8g2.setFont(FONT_S);
  do {
    //3行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawUTF8(AC(input1), u8g2.getMaxCharHeight(), input1);
    u8g2.drawUTF8(AC(input2), AM(), input2);
    u8g2.drawUTF8(AC(input3), LCDHeight, input3);
  } while (u8g2.nextPage());
}

void refreshOLED(char* input1, char* input2, char* input3, const uint8_t* font) {
  u8g2.firstPage();
  u8g2.setFont(font);
  do {
    //3行
    //FONT_M = u8g2_font_fub14_tn;
    u8g2.drawUTF8(AC(input1), u8g2.getMaxCharHeight(), input1);
    u8g2.drawUTF8(AC(input2), AM(), input2);
    u8g2.drawUTF8(AC(input3), LCDHeight, input3);
  } while (u8g2.nextPage());
}

// macros from DateTime.h
/* Useful Constants */
#define SECS_PER_MIN (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY (SECS_PER_HOUR * 24L)
/* Useful Macros for getting elapsed time */
#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN)
#define numberOfHours(_time_) ((_time_ % SECS_PER_DAY) / SECS_PER_HOUR)
#define elapsedDays(_time_) (_time_ / SECS_PER_DAY)

char* sec2minsec(unsigned long n) {
  //手冲称使用xx:yy显示时间
  int minute = 0;
  int second = 0;
  if (n < 99 * 60 + 60) {
    if (n >= 60) {
      minute = n / 60;
      n = n % 60;
    }
    second = n;
  } else {
    minute = 99;
    second = 59;
  }
  snprintf(minsec, 6, "%02d:%02d", minute, second);
  return (char*)minsec;
}

char* sec2sec(unsigned long n) {
  //意式称使用xx秒显示时间
  unsigned long second = 0;
  // if (n < 99) {
  //   second = n;
  // } else
  //   second = 99;
  second = n;
  snprintf(minsec, 6, "%ds", second);
  // Serial.print("minsec ");
  // Serial.println(minsec);
  return (char*)minsec;
}

//自定义trim消除空格
char* ltrim(char* s) {
  while (isspace(*s)) s++;
  return s;
}

char* rtrim(char* s) {
  char* back = s + strlen(s);
  while (isspace(*--back))
    ;
  *(back + 1) = '\0';
  return s;
}

char* trim(char* s) {
  return rtrim(ltrim(s));
}

#endif