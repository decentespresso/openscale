#ifndef POWER_H
#define POWER_H
#include <Arduino.h>
#include "so_declare.h"
#include "so_parameter.h"
#include "so_display.h"
#include "so_gyro.h"
#include "so_espnow.h"

const int windowSize = 1000;
float batteryLevels[windowSize];
int readIndex = 0;
// ADC Characteristics
const float batteryMaxVoltage = 4.2;  // Maximum voltage of battery
const float showFullBatteryAboveVoltage = 4.1;  // Maximum voltage of battery
const float showEmptyBatteryBelowVoltage = 3.4;  // Maximum voltage of battery


const float dividerRatio = 2.0;       // Ratio of voltage divider (100kΩ/100kΩ)
const float adcResolution = 4095.0;   // ESP32 ADC resolution for 12-bit (0-4095)
const float referenceVoltage = 3.3;   // Reference voltage of ESP32 ADC

// Low battery threshold
const float lowBatteryThreshold = 3.3;  // Battery voltage threshold for low battery action

void (*resetFunc)(void) = 0;  //AVR重启函数

void reset() {
#ifdef ESP32
  ESP.restart();
#endif  // ESP32
#ifdef AVR
  resetFunc();
#endif  // AVR
#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
  NVIC_SystemReset();
#endif  // defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
}

#ifdef ESP32
void esp32_wakeup() {
#ifndef ESP32C3
  detachInterrupt(GPIO_NUM_BUTTON_POWER);
#endif
  u8g2.setPowerSave(0);
  scale.powerUp();
  mpu.enableCycle(false);
}

void esp32_sleep() {
  //beep(4, 50);
  u8g2.setPowerSave(1);
  mpu.enableCycle(true);
  scale.powerDown();
#ifdef ESP32C3
  esp_deep_sleep_enable_gpio_wakeup(1 << GPIO_NUM_BUTTON_POWER, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  attachInterrupt(GPIO_NUM_BUTTON_POWER, esp32_wakeup, FALLING);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_BUTTON_POWER, LOW);
#endif
  esp_deep_sleep_start();
}
#endif  //ESP32


void shut_down_now() {
  Serial.println("power off");
  buzzer.beep(4, 50);
#ifdef CHINESE
  refreshOLED((char*)"关机", FONT_M);
#endif
#ifdef ENGLISH
  refreshOLED((char*)"Power off", FONT_M);
#endif
#ifdef ESPNOW
  if (b_f_espnow) {
    b_f_power_off = 1;
    updateEspnow(1);
  }
#endif
  delay(1000);
#ifdef ESP32
  esp32_sleep();
#else
  pinMode(BUTTON_KEY, OUTPUT);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, OUTPUT);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(1000);
#endif  //ESP32
}

void shut_down_low_battery(float voltage) {
  Serial.print("Low battery, voltage:");
  Serial.println(voltage);
#ifdef CHINESE
  refreshOLED((char*)"电量不足", FONT_M);
#endif
#ifdef ENGLISH
  refreshOLED((char*)"Low battery", FONT_M);
#endif
#ifdef ESPNOW
  if (b_f_espnow) {
    b_f_power_off = 1;
    updateEspnow(1);
  }
#endif
  delay(1000);
#ifdef ESP32
  esp32_sleep();
#else
  pinMode(BUTTON_KEY, OUTPUT);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, OUTPUT);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(1000);
#endif
}

void shut_down_now_nobeep() {
  Serial.println("power off no beep");
#ifdef CHINESE
  refreshOLED((char*)"关机", FONT_M);
#endif
#ifdef ENGLISH
  refreshOLED((char*)"Power off", FONT_M);
#endif
#ifdef ESPNOW
  if (b_f_espnow) {
    b_f_power_off = 1;
    updateEspnow(1);
  }
#endif
  delay(1000);
#ifdef ESP32
  esp32_sleep();
#else
  pinMode(BUTTON_KEY, OUTPUT);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, OUTPUT);
  delay(BUTTON_KEY_DELAY);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(1000);
#endif
}

void shut_down_now_accidentTouch() {
  Serial.println("accdient on, power off...");
#ifdef ESPNOW
  if (b_f_espnow) {
    b_f_power_off = 1;
    updateEspnow(1);
  }
#endif
#ifdef ESP32
  esp32_sleep();
#else
  pinMode(BUTTON_KEY, OUTPUT);
  delay(300);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(300);
  pinMode(BUTTON_KEY, OUTPUT);
  delay(300);
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  delay(1000);
#endif
}

float getVoltage(int batteryPin) {
  int adcValue = analogRead(batteryPin);                               // Read the value from ADC
  float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;  // Calculate voltage at ADC pin
  float batteryVoltage = voltageAtPin * dividerRatio;                  // Calculate the actual battery voltage
  float correctedVoltage = batteryVoltage * f_batteryCalibrationFactor;
  return correctedVoltage;
}

void power_off(int min) {
  if (getVoltage(BATTERY_PIN) < BATTERY_VOLTAGE_MIN) {
    //shut_down_low_battery(getVoltage(BATTERY_PIN));
    return;
  }

  if (!b_f_is_charging) {
    if (min == -1) {
      t_power_off = millis();
      //Serial.println("power off timer reset");
    }
    if (min > 0) {
      double d_timeleft = min * 60 - (millis() - t_power_off) / 1000;
      // Serial.print(d_timeleft);
      // Serial.println(" seconds to power off");
      if (d_timeleft <= 0) {
        shut_down_now();
      }
    }
  }
}

void power_off_gyro(int sec) {
  if (!b_f_is_charging) {
    if (sec == -1) {
      t_power_off_gyro = millis();
      //Serial.println("power off by gyro timer reset");
    }
    if (sec > 0) {
      double d_timeleft = sec - (millis() - t_power_off_gyro) / 1000;
      //Serial.print(d_timeleft);
      //Serial.println(" seconds to power off by gyro");
      if (d_timeleft <= 0) {
        shut_down_now();
      }
    }
  }
}

void power_off(double sec) {
  if (!b_f_is_charging) {
    if (sec == -1) {
      t_power_off = millis();
      //Serial.println("power off timer reset");
    }
    if (sec > 0) {
      double d_timeleft = sec - (millis() - t_power_off) / 1000;
      //Serial.print(d_timeleft);
      //Serial.println(" seconds to power off");
      if (d_timeleft <= 0) {
        shut_down_now();
      }
    }
  }
}

#ifdef CHECKBATTERY
float get_usb_voltage() {
  Serial.print("get_usb_voltage");
  Serial.println(analogRead(USB_PIN) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor);
  return analogRead(USB_PIN) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor;
}

float get_bat_voltage() {
  Serial.print("get_bat_voltage");
  Serial.println(analogRead(USB_PIN) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor);
  return analogRead(BATTERY_PIN) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor;
}

#endif  //CHECKBATTERY

void checkBattery() {
  // Serial.print("Battery Voltage:");
  // Serial.print(getVoltage(BATTERY_PIN));
  float perc = map(getVoltage(BATTERY_PIN) * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);//map funtion doesn't take float as input.
  // Serial.print(" ");
  // Serial.print(perc);
  // Serial.print("\%,");
  i_icon = map(perc, 0, 100, 1, 5);
  // Serial.print(" i_icon =");
  // Serial.println(i_icon);
  switch (i_icon) {
    case 0:
      c_battery = (char*)"0";
      break;
    case 1:
      c_battery = (char*)"1";
      break;
    case 2:
      c_battery = (char*)"2";
      break;
    case 3:
      c_battery = (char*)"3";
      break;
    case 4:
      c_battery = (char*)"4";
      break;
    case 5:
      c_battery = (char*)"5";
      break;
    default:
      c_battery = (char*)"5";
      break;
  }
  // if (getVoltage(BATTERY_PIN) > 4.20) {
  //   b_f_is_charging = true;
  //   //Serial.println("is charging");
  // }
}
#endif