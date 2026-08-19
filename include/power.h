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

#ifdef ESP32
#include "driver/rtc_io.h"
#endif

#ifdef ADS1115ADC
#include <Adafruit_ADS1X15.h>
Adafruit_ADS1115 ads;
#endif

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex

#define PIN_BITMASK (BUTTON_PIN_BITMASK((gpio_num_t)BUTTON_SQUARE) | BUTTON_PIN_BITMASK((gpio_num_t)BUTTON_CIRCLE) | BUTTON_PIN_BITMASK((gpio_num_t)BATTERY_CHARGING))

void sendBlePowerOff(int i_reason);
#if HDS_FEATURE_WEBSOCKET
void sendWebsocketPowerOff(int i_reason);
#endif
void bleShutdown();
#if HDS_FEATURE_WIFI
void stopWifi();
#endif
#if HDS_FEATURE_WEBSERVER
void stopWebServer();
#elif HDS_FEATURE_WIFI
static void stopWifiConfigServer();
#endif
#if HDS_ENABLE_GRINDER
void beforeDeepSleepFlush();
#endif

const int windowSize = 1000;
float batteryLevels[windowSize];
int readIndex = 0;
const float showFullBatteryAboveVoltage = 4.1;
const float showEmptyBatteryBelowVoltage = 3.4;

#if defined(V7_2)
const float dividerRatio = (100.0 + 33.0) / 100.0;
#else  //7_5 and else
const float dividerRatio = (100.0 + 100.0) / 100.0;
#endif

const float adcResolution = 4095.0;

const float referenceVoltage = 3.3;

const float lowBatteryThreshold = 3.2;

void (*resetFunc)(void) = 0;

void reset() {
#ifdef ESP32
  bleShutdown();
#if HDS_FEATURE_WEBSERVER
  stopWebServer();
#elif HDS_FEATURE_WIFI
  stopWifiConfigServer();
#endif
#if HDS_FEATURE_WIFI
  stopWifi();
#endif
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
  } else {
    b_ads1115InitFail = false;
    ads.setGain(GAIN_ONE);
    ads.setDataRate(RATE_ADS1115_860SPS);
  }
}
#endif

#ifdef ESP32

int i_wakeupPin;
void configureWakePinForDeepSleep(gpio_num_t pin) {
  rtc_gpio_init(pin);
  rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(pin);
  rtc_gpio_pulldown_dis(pin);
}

void configureWakePinsForDeepSleep() {
  configureWakePinForDeepSleep((gpio_num_t)BUTTON_CIRCLE);
  configureWakePinForDeepSleep((gpio_num_t)BUTTON_SQUARE);
  configureWakePinForDeepSleep((gpio_num_t)BATTERY_CHARGING);
}

void releaseWakePinsFromRtcMode() {
  rtc_gpio_deinit((gpio_num_t)BUTTON_CIRCLE);
  rtc_gpio_deinit((gpio_num_t)BUTTON_SQUARE);
  rtc_gpio_deinit((gpio_num_t)BATTERY_CHARGING);
}

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;
    default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    GPIO_reason = esp_sleep_get_ext1_wakeup_status();

    Serial.print("Raw bitmask value returned: ");
    Serial.println(GPIO_reason);

    Serial.print("GPIO that triggered the wake up calculated using log method: ");
    i_wakeupPin = log(GPIO_reason) / log(2);
    Serial.println(i_wakeupPin);

    Serial.print("GPIO that triggered the wake up using built in definitions: ");
    switch (GPIO_reason) {
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING):
        GPIO_power_on_with = BATTERY_CHARGING;
        Serial.println("Only GPIO " + String(BATTERY_CHARGING));
        break;
      case BUTTON_PIN_BITMASK(BUTTON_SQUARE):
        GPIO_power_on_with = BUTTON_SQUARE;
        Serial.println("Only GPIO " + String(BUTTON_SQUARE));
        break;
      case BUTTON_PIN_BITMASK(BUTTON_CIRCLE):
        GPIO_power_on_with = BUTTON_CIRCLE;
        Serial.println("Only GPIO " + String(BUTTON_CIRCLE));
        break;
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING) | BUTTON_PIN_BITMASK(BUTTON_SQUARE):
        GPIO_power_on_with = BUTTON_SQUARE;
        Serial.println("Both GPIO " + String(BATTERY_CHARGING) + " + " + String(BUTTON_SQUARE));
        break;
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING) | BUTTON_PIN_BITMASK(BUTTON_CIRCLE):
        GPIO_power_on_with = BUTTON_CIRCLE;
        Serial.println("Both GPIO " + String(BATTERY_CHARGING) + " + " + String(BUTTON_CIRCLE));
        break;
      case BUTTON_PIN_BITMASK(BUTTON_SQUARE) | BUTTON_PIN_BITMASK(BUTTON_CIRCLE):
        GPIO_power_on_with = BUTTON_SQUARE;
        Serial.println("Both GPIO " + String(BUTTON_SQUARE) + " + " + String(BUTTON_CIRCLE));
        break;
      case BUTTON_PIN_BITMASK(BATTERY_CHARGING) | BUTTON_PIN_BITMASK(BUTTON_SQUARE) | BUTTON_PIN_BITMASK(BUTTON_CIRCLE):
        GPIO_power_on_with = BUTTON_SQUARE;
        Serial.println("All GPIO " + String(BATTERY_CHARGING) + " + " + String(BUTTON_SQUARE) + " + " + String(BUTTON_CIRCLE));
        break;
      default: Serial.println("Unknown pin"); break;
    }
  }
}


void esp32_sleep() {
#if HDS_ENABLE_GRINDER
  beforeDeepSleepFlush();
#endif
  bleShutdown();
#if HDS_FEATURE_WEBSERVER
  stopWebServer();
#elif HDS_FEATURE_WIFI
  stopWifiConfigServer();
#endif
#if HDS_FEATURE_WIFI
  stopWifi();
#endif
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
  configureWakePinsForDeepSleep();
  esp_sleep_enable_ext1_wakeup_io(PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_LOW);
#endif


  pinMode(OLED_SDIN, OUTPUT);  digitalWrite(OLED_SDIN, LOW);
  pinMode(OLED_SCLK, OUTPUT);  digitalWrite(OLED_SCLK, LOW);
  pinMode(OLED_DC, OUTPUT);    digitalWrite(OLED_DC, LOW);
  pinMode(OLED_RST, OUTPUT);   digitalWrite(OLED_RST, LOW);
  pinMode(OLED_CS, OUTPUT);    digitalWrite(OLED_CS, LOW);
  gpio_hold_en((gpio_num_t)OLED_SDIN);
  gpio_hold_en((gpio_num_t)OLED_SCLK);
  gpio_hold_en((gpio_num_t)OLED_DC);
  gpio_hold_en((gpio_num_t)OLED_RST);
  gpio_hold_en((gpio_num_t)OLED_CS);

  pinMode(SCALE_SCLK, OUTPUT); digitalWrite(SCALE_SCLK, LOW);
  pinMode(SCALE_PDWN, OUTPUT); digitalWrite(SCALE_PDWN, LOW);
  pinMode(SCALE_DOUT, INPUT);
  gpio_hold_en((gpio_num_t)SCALE_SCLK);
  gpio_hold_en((gpio_num_t)SCALE_PDWN);
  gpio_hold_en((gpio_num_t)SCALE_DOUT);

  pinMode(SCALE2_SCLK, OUTPUT); digitalWrite(SCALE2_SCLK, LOW);
  pinMode(SCALE2_PDWN, OUTPUT); digitalWrite(SCALE2_PDWN, LOW);
  pinMode(SCALE2_DOUT, INPUT);
  gpio_hold_en((gpio_num_t)SCALE2_SCLK);
  gpio_hold_en((gpio_num_t)SCALE2_PDWN);
  gpio_hold_en((gpio_num_t)SCALE2_DOUT);

  pinMode(I2C_SCL, OUTPUT); digitalWrite(I2C_SCL, LOW);
  pinMode(I2C_SDA, OUTPUT); digitalWrite(I2C_SDA, LOW);
  gpio_hold_en((gpio_num_t)I2C_SCL);
  gpio_hold_en((gpio_num_t)I2C_SDA);

  digitalWrite(ACC_PWR_CTRL, LOW);
  gpio_hold_en((gpio_num_t)ACC_PWR_CTRL);
  digitalWrite(PWR_CTRL, LOW);
  gpio_hold_en((gpio_num_t)PWR_CTRL);
  gpio_deep_sleep_hold_en();
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
  if (t_batteryRefresh > 0){
    Serial.print("Low battery, voltage:");
    Serial.println(voltage);
    refreshOLED((char*)"Low battery", FONT_M);
#ifdef ESPNOW
    if (b_espnow) {
      b_power_off = 1;
      updateEspnow(1);
    }
#endif
    sendBlePowerOff(3);
#if HDS_FEATURE_WEBSOCKET
    sendWebsocketPowerOff(3);
#endif
#ifdef BUZZER
    buzzer.off();
#endif
    delay(1000);
    esp32_sleep();
  }
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

void updateBattery(int batteryPin){
  if (!b_ads1115InitFail) {
    int16_t adc0;
    float volts0;
    adc0 = ads.readADC_SingleEnded(0);
    volts0 = ads.computeVolts(adc0);
    f_batteryVoltage = volts0 * 2.0;
  }
  else {
    int adcValue = analogRead(batteryPin);
    float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;
    float batteryVoltage = voltageAtPin * dividerRatio;
    float correctedVoltage = batteryVoltage * f_batteryCalibrationFactor;
    f_batteryVoltage = correctedVoltage;
  }
  t_batteryRefresh = millis();
#if HDS_ENABLE_ENERGY_MENU
  energyRuntime.batterySampleSequence++;
#endif
}

float getUsbVoltage(int usbPin) {
  int adcValue = analogRead(usbPin);
  float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;
  float usbVoltage = voltageAtPin * 2.0;
  return usbVoltage;
}

int i_lowBatteryCount = 0;
int i_lowBatteryCountTotal = 0;
#if HDS_ENABLE_ENERGY_MENU
bool processLegacyLowBattery() {
  if (!b_softSleep) {
    if (f_batteryVoltage < lowBatteryThreshold) {
      updateBattery(BATTERY_PIN);
    }
    if (f_batteryVoltage > lowBatteryThreshold) {
      i_lowBatteryCount = 0;
    }
    if (f_batteryVoltage < lowBatteryThreshold) {
      i_lowBatteryCount++;
      i_lowBatteryCountTotal++;
    }
    const bool confirmed = EnergyRuntimePolicy::lowBatteryConfirmed(i_lowBatteryCount, false);
    if (!confirmed) return false;
    shut_down_low_battery(f_batteryVoltage);
    return true;
  }
  return false;
}

bool processNewBatterySample() {
  if (!energyRuntime.batterySamples.shouldEvaluate(energyRuntime.batterySampleSequence)) {
    return false;
  }
  if (b_is_charging || f_batteryVoltage > lowBatteryThreshold) {
    i_lowBatteryCount = 0;
  } else if (f_batteryVoltage < lowBatteryThreshold) {
    i_lowBatteryCount++;
    i_lowBatteryCountTotal++;
    if (!EnergyRuntimePolicy::lowBatteryConfirmed(i_lowBatteryCount, true) &&
        i_batteryRefreshTareInterval > 1000) {
      t_batteryRefresh = millis() - (i_batteryRefreshTareInterval - 1000);
    }
  }
  const bool confirmed = EnergyRuntimePolicy::lowBatteryConfirmed(i_lowBatteryCount, true);
  if (confirmed) {
    shut_down_low_battery(f_batteryVoltage);
  }
  return confirmed;
}

void evaluateAutoOff(double seconds, bool showCountdown) {
  const unsigned long now = millis();
  const bool cadenceEnabled = energyPolicy.featureEnabled(EnergyFeature::PowerCadence);
  if (cadenceEnabled) {
    if (!energyRuntime.schedule.autoOff.shouldRun(true, now, 1000)) {
      return;
    }
  }
  const double timeLeft = seconds - (now - t_power_off) / 1000;
  if (showCountdown && !energyPolicy.featureEnabled(EnergyFeature::SerialQuiet)) {
    Serial.print(timeLeft);
    Serial.println(" seconds to power off");
  }
  if (timeLeft <= 0 && b_autoSleep) {
    shut_down_now();
  }
}
#endif

void power_off(int min) {
#if HDS_ENABLE_ENERGY_MENU
  const bool cadenceEnabled = energyPolicy.featureEnabled(EnergyFeature::PowerCadence);
  if (cadenceEnabled && processNewBatterySample()) return;
  if (!cadenceEnabled && !b_is_charging && processLegacyLowBattery()) return;
  if (min == -1 && (cadenceEnabled || !b_is_charging)) {
    t_power_off = millis();
  } else if (min > 0 && !b_is_charging) {
    evaluateAutoOff(min * 60.0, true);
  }
#else
  if (!b_is_charging) {
    if (!b_softSleep) {
      if (f_batteryVoltage < lowBatteryThreshold) {
        updateBattery(BATTERY_PIN);
      }
      if (f_batteryVoltage > lowBatteryThreshold) {
        i_lowBatteryCount = 0;
      }

      if (f_batteryVoltage < lowBatteryThreshold) {
        i_lowBatteryCount++;
        i_lowBatteryCountTotal++;
      }

      if (i_lowBatteryCount > 50) {
        shut_down_low_battery(f_batteryVoltage);
        return;
      }
    }

    if (min == -1) {
      t_power_off = millis();
    }
    if (min > 0) {
      double d_timeleft = min * 60 - (millis() - t_power_off) / 1000;
      Serial.print(d_timeleft);
      Serial.println(" seconds to power off");
      if (d_timeleft <= 0 && b_autoSleep == true) {
        shut_down_now();
      }
    }
  }
#endif
}

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void power_off_gyro(int sec) {
  if (!b_is_charging) {
    if (sec == -1) {
      t_power_off_gyro = millis();
    }
    if (sec > 0) {
      double d_timeleft = sec - (millis() - t_power_off_gyro) / 1000;
      if (d_timeleft <= 0) {
        sendBlePowerOff(4);
#if HDS_FEATURE_WEBSOCKET
        sendWebsocketPowerOff(4);
#endif
        shut_down_now();
      }
    }
  }
}
#endif

void power_off(double sec) {
#if HDS_ENABLE_ENERGY_MENU
  const bool cadenceEnabled = energyPolicy.featureEnabled(EnergyFeature::PowerCadence);
  if (cadenceEnabled && processNewBatterySample()) return;
  if (!cadenceEnabled && !b_is_charging && processLegacyLowBattery()) return;
  if (sec == -1 && (cadenceEnabled || !b_is_charging)) {
    t_power_off = millis();
  } else if (sec > 0 && !b_is_charging) {
    evaluateAutoOff(sec, false);
  }
#else
  if (!b_is_charging) {
    if (!b_softSleep) {
      if (f_batteryVoltage < lowBatteryThreshold) {
        updateBattery(BATTERY_PIN);
      }
      if (f_batteryVoltage > lowBatteryThreshold) {
        i_lowBatteryCount = 0;
      }

      if (f_batteryVoltage < lowBatteryThreshold) {
        i_lowBatteryCount++;
        i_lowBatteryCountTotal++;
      }

      if (i_lowBatteryCount > 50) {
        shut_down_low_battery(f_batteryVoltage);
        return;
      }
    }

    if (sec == -1) {
      t_power_off = millis();
    }
    if (sec > 0) {
      double d_timeleft = sec - (millis() - t_power_off) / 1000;
      if (d_timeleft <= 0 && b_autoSleep == true) {
        shut_down_now();
      }
    }
  }
#endif
}

#ifdef CHECKBATTERY
float readCheckBatteryVoltage(int pin) {
  return analogRead(pin) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor;
}

float get_usb_voltage() {
  Serial.print("get_usb_voltage");
  Serial.println(readCheckBatteryVoltage(USB_PIN));
  return readCheckBatteryVoltage(USB_PIN);
}

float get_bat_voltage() {
  Serial.print("get_bat_voltage");
  Serial.println(readCheckBatteryVoltage(BATTERY_PIN));
  return readCheckBatteryVoltage(BATTERY_PIN);
}

#endif  //CHECKBATTERY

void checkBattery() {
  float perc = map(f_batteryVoltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);
#if defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  if (digitalRead(USB_DET) == LOW) {
#else
  if (digitalRead(BATTERY_CHARGING) == LOW) {
#endif
    b_is_charging = true;
    c_battery = (char*)"6";
  } else {
    b_is_charging = false;
    if (perc <= 5) {
      c_battery = (char*)"0";
    } else if (perc > 5 && perc <= 20) {
      c_battery = (char*)"1";
    } else if (perc > 20 && perc <= 40) {
      c_battery = (char*)"2";
    } else if (perc > 40 && perc <= 60) {
      c_battery = (char*)"3";
    } else if (perc > 60 && perc <= 80) {
      c_battery = (char*)"4";
    } else if (perc > 80) {
      c_battery = (char*)"5";
    }
  }
}
#endif
