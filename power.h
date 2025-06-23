#ifndef POWER_H
#define POWER_H
#include <Arduino.h>
#include "declare.h"
#include "parameter.h"
#include "display.h"
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
#include "gyro.h"
#endif
#include "espnow.h"

#ifdef ADS1115ADC
#include <Adafruit_ADS1X15.h>
Adafruit_ADS1115 ads;  // Create an ADS1115 object
#endif

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex

#define PIN_BITMASK (BUTTON_PIN_BITMASK((gpio_num_t)BUTTON_SQUARE) | BUTTON_PIN_BITMASK((gpio_num_t)BUTTON_CIRCLE) | BUTTON_PIN_BITMASK((gpio_num_t)BATTERY_CHARGING))

//prototype
void sendBlePowerOff(int i_reason);

const int windowSize = 1000;
float batteryLevels[windowSize];
int readIndex = 0;
// ADC Characteristics
//const float batteryMaxVoltage = 4.2;             // Maximum voltage of battery
const float showFullBatteryAboveVoltage = 4.1;   // Maximum voltage of battery
const float showEmptyBatteryBelowVoltage = 3.4;  // Minimum voltage of battery

// Voltage divider configuration: Vbattery → 33kΩ resistor → ESP32 ADC → 100kΩ resistor → GND
// Formula for calculating the battery voltage (Vbattery):
// Vadc = Vbattery * (100 / (100 + 33)), therefore
// Vbattery = Vadc * ((100 + 33) / 100)
#if defined(V7_2)
const float dividerRatio = (100.0 + 33.0) / 100.0;
#else  //7_5 and else
const float dividerRatio = (100.0 + 100.0) / 100.0;
#endif

// ESP32 ADC resolution for 12-bit (0-4095)
const float adcResolution = 4095.0;

// Reference voltage for the ESP32 ADC
const float referenceVoltage = 3.3;

// Low battery threshold
const float lowBatteryThreshold = 3.2;  // Battery voltage threshold for low battery action

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

#ifdef ADS1115ADC
void ADS_init() {
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS1115!");
    b_ads1115InitFail = true;
    // while (1) {
    //   Serial.println("Failed to initialize ADS1115!");
    //   delay(5000);
    // }
  } else {
    ads.setGain(GAIN_ONE);  // +/- 4_096V range
    ads.setDataRate(RATE_ADS1115_860SPS);
  }
}
#endif

#ifdef ESP32

int i_wakeupPin;
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  // Print if if it was a GPIO or something else
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;
    default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  // If it is due to the a GPIO pin find out which one/s
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    GPIO_reason = esp_sleep_get_ext1_wakeup_status();

    // Print the raw value returned by esp_sleep_get_ext1_wakeup_status. This is the bitmask of the pin/s that triggered wake up
    Serial.print("Raw bitmask value returned: ");
    Serial.println(GPIO_reason);

    // Using log method to work out trigger pin. This is the method used by Random Nerd Tutorials
    Serial.print("GPIO that triggered the wake up calculated using log method: ");
    i_wakeupPin = log(GPIO_reason) / log(2);
    Serial.println(i_wakeupPin);

    // Use defined pins bitmask to find which pin/s triggered wakeup
    Serial.print("GPIO that triggered the wake up using built in definitions: ");
    switch (GPIO_reason) {
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING):
        GPIO_power_on_with = BATTERY_CHARGING;
        Serial.println("Only GPIO " + String(BATTERY_CHARGING));
        break;  // GPIO 27
      case BUTTON_PIN_BITMASK(BUTTON_SQUARE):
        GPIO_power_on_with = BUTTON_SQUARE;
        Serial.println("Only GPIO " + String(BUTTON_SQUARE));
        break;
      case BUTTON_PIN_BITMASK(BUTTON_CIRCLE):
        GPIO_power_on_with = BUTTON_CIRCLE;
        Serial.println("Only GPIO " + String(BUTTON_CIRCLE));
        break;                                                                                                                                                                                                                                           // GPIO 33
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING) | BUTTON_PIN_BITMASK(BUTTON_SQUARE): Serial.println("Both GPIO " + String(BATTERY_CHARGING) + " + " + String(BUTTON_SQUARE)); break;                                                                     // GPIO 27 + 32
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING) | BUTTON_PIN_BITMASK(BUTTON_CIRCLE): Serial.println("Both GPIO " + String(BATTERY_CHARGING) + " + " + String(BUTTON_CIRCLE)); break;                                                                     // GPIO 27 + 33
      case BUTTON_PIN_BITMASK(BUTTON_SQUARE) | BUTTON_PIN_BITMASK(BUTTON_CIRCLE): Serial.println("Both GPIO " + String(BUTTON_SQUARE) + " + " + String(BUTTON_CIRCLE)); break;                                                                           // GPIO 32 + 33
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING) | BUTTON_PIN_BITMASK(BUTTON_SQUARE) | BUTTON_PIN_BITMASK(BUTTON_CIRCLE): Serial.println("All GPIO " + String(BATTERY_CHARGING) + " + " + String(BUTTON_SQUARE) + " + " + String(BUTTON_CIRCLE)); break;  // GPIO 27 + 32 + 33
      default: Serial.println("Unknown pin"); break;
    }
  }
}

// void esp32_wakeup() {
// #ifndef ESP32C3
//   detachInterrupt(GPIO_NUM_BUTTON_POWER);
// #endif
//   u8g2.setPowerSave(0);
//   scale.powerUp();
//   mpu.enableSleep(false);
//   mpu.enableCycle(true);
// }

void esp32_sleep() {
  //beep(4, 50);
  u8g2.setPowerSave(1);
#ifdef ACC_MPU6050
  if (b_gyroEnabled) {
    mpu.enableCycle(false);
    mpu.enableSleep(true);
  }
#endif
  scale.powerDown();
#ifdef ESP32C3
  esp_deep_sleep_enable_gpio_wakeup(1 << GPIO_NUM_BUTTON_POWER, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  //old sleep wakeup
  // attachInterrupt(GPIO_NUM_BUTTON_POWER, esp32_wakeup, FALLING);
  // esp_sleep_enable_ext0_wakeup(GPIO_NUM_BUTTON_POWER, LOW);
  //new bitmap sleep wakeup pin
  esp_sleep_enable_ext1_wakeup_io(PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  //#if defined(V7_3) || defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  digitalWrite(ACC_PWR_CTRL, LOW);
  gpio_hold_en((gpio_num_t)ACC_PWR_CTRL);
  //#endif
  digitalWrite(PWR_CTRL, LOW);
  gpio_hold_en((gpio_num_t)PWR_CTRL);
  esp_deep_sleep_start();
}
#endif  //ESP32


void shut_down_now() {
  Serial.println("power off");
#ifdef BUZZER
  buzzer.beep(1, BUZZER_DURATION);
#endif
  u8g2.setFontDirection(0);
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);
  refreshOLED((char*)"Off", FONT_M);
#ifdef ESPNOW
  if (b_espnow) {
    b_power_off = 1;
    updateEspnow(1);
  }
#endif
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  esp32_sleep();
}


void shut_down_low_battery(float voltage) {
  Serial.print("Low battery, voltage:");
  Serial.println(voltage);
  refreshOLED((char*)"Low battery", FONT_M);
#ifdef ESPNOW
  if (b_espnow) {
    b_power_off = 1;
    updateEspnow(1);
  }
  sendBlePowerOff(3);
#endif
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  esp32_sleep();
}

void shut_down_now_nobeep() {
  Serial.println("power off no beep");
  u8g2.setFontDirection(0);
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);
  refreshOLED((char*)"Off", FONT_M);
#ifdef ESPNOW
  if (b_espnow) {
    b_power_off = 1;
    updateEspnow(1);
  }
#endif
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  esp32_sleep();
}

void shut_down_now_accidentTouch() {
  Serial.println("accdient on, power off...");
#ifdef ESPNOW
  if (b_espnow) {
    b_power_off = 1;
    updateEspnow(1);
  }
#endif
  esp32_sleep();
}

float getVoltage(int batteryPin) {
  //#ifdef ADS1115ADC
  if (!b_ads1115InitFail) {
    int16_t adc0;
    float volts0;
    adc0 = ads.readADC_SingleEnded(0);
    volts0 = ads.computeVolts(adc0);
    return volts0 * 2.0;
  }
  //#else
  else {
    int adcValue = analogRead(batteryPin);                               // Read the value from ADC
    float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;  // Calculate voltage at ADC pin
    float batteryVoltage = voltageAtPin * dividerRatio;                  // Calculate the actual battery voltage
    float correctedVoltage = batteryVoltage * f_batteryCalibrationFactor;
    return correctedVoltage;
  }
  //#endif
}

float getUsbVoltage(int usbPin) {
  int adcValue = analogRead(usbPin);                                   // Read the value from ADC
  float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;  // Calculate voltage at ADC pin
  float usbVoltage = voltageAtPin * 2.0;                               // Calculate the actual battery voltage
  //float correctedVoltage = usbVoltage * f_batteryCalibrationFactor;
  return usbVoltage;
}

int i_lowBatteryCount = 0;
int i_lowBatteryCountTotal = 0;
void power_off(int min) {
  if (!b_is_charging) {
    if (getVoltage(BATTERY_PIN) > lowBatteryThreshold) {
      i_lowBatteryCount = 0;
    }

    if (getVoltage(BATTERY_PIN) < lowBatteryThreshold) {
      i_lowBatteryCount++;
      i_lowBatteryCountTotal++;
    }

    if (i_lowBatteryCount > 50) {
      shut_down_low_battery(getVoltage(BATTERY_PIN));
      return;
    }

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

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void power_off_gyro(int sec) {
  if (!b_is_charging) {
    if (sec == -1) {
      t_power_off_gyro = millis();
      //Serial.println("power off by gyro timer reset");
    }
    if (sec > 0) {
      double d_timeleft = sec - (millis() - t_power_off_gyro) / 1000;
      //Serial.print(d_timeleft);
      //Serial.println(" seconds to power off by gyro");
      if (d_timeleft <= 0) {
        sendBlePowerOff(4);
        shut_down_now();
      }
    }
  }
}
#endif

void power_off(double sec) {
  if (!b_is_charging) {
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
  float perc = map(getVoltage(BATTERY_PIN) * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);  //map funtion doesn't take float as input.
#if defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  //if (getUsbVoltage(USB_DET) > 4.0) {
  if (digitalRead(USB_DET) == LOW) {
#else
  if (digitalRead(BATTERY_CHARGING) == LOW) {
#endif
    b_is_charging = true;
    c_battery = (char*)"6";  // Special icon for charging
    // Serial.println("Battery is charging");
  } else {
    b_is_charging = false;
    //power_off_gyro(-1);  //restart gyro timer
    // Serial.print("Battery is ");
    // Serial.print(perc);
    // Serial.println("\%");
    if (perc <= 5) {
      c_battery = (char*)"0";  // 0% or very low battery
    } else if (perc > 5 && perc <= 20) {
      c_battery = (char*)"1";  // 6-20% battery
    } else if (perc > 20 && perc <= 40) {
      c_battery = (char*)"2";  // 21-40% battery
    } else if (perc > 40 && perc <= 60) {
      c_battery = (char*)"3";  // 41-60% battery
    } else if (perc > 60 && perc <= 80) {
      c_battery = (char*)"4";  // 61-80% battery
    } else if (perc > 80) {
      c_battery = (char*)"5";  // 81-100% battery
    }
  }
}
#endif