#include <Arduino.h>
#include <cstring>
#include "config.h"

#include "parameter.h"
#include "storage.h"
#include "power.h"
#include "gyro.h"
#include "display.h"
#include "declare.h"
#if HDS_FEATURE_WIFI
#include "wifi_setup.h"
#include <WiFi.h>
#endif
#if HDS_FEATURE_WEBSERVER
#include "webserver.h"
#elif HDS_FEATURE_WIFI
#include "wifi_config_server.h"
#endif
#include "websocket.h"
#if HDS_FEATURE_ELEGANT_OTA
#include "wifi_ota.h"
#endif
#if HDS_ENABLE_GRINDER
#include "grinder_runtime.h"
#endif
#if HDS_FEATURE_PULL_OTA
#include "pull_ota.h"
#include "ota_rollback.h"
#endif


#include "menu.h"
#include "ble.h"
#include "usbcomm.h"
#include "finger_detection.h"

#if HDS_ENABLE_GRINDER
#ifndef GRINDER_MENU_CHORD_HOLD_MS
#define GRINDER_MENU_CHORD_HOLD_MS 500
#endif
#endif

bool anyScaleButtonPressed() {
  return digitalRead(BUTTON_CIRCLE) == LOW || digitalRead(BUTTON_SQUARE) == LOW;
}

bool buttonChecksSuppressedUntilRelease() {
  if (!b_buttonChordSuppressUntilRelease) {
    return false;
  }
  if (!anyScaleButtonPressed()) {
    b_buttonChordSuppressUntilRelease = false;
  }
  return true;
}

void adsDebugCallback(const ADS1232DebugInfo& info) {
  static unsigned long lastDebugPrint = 0;
  unsigned long now = millis();

  if (now - lastDebugPrint >= 1000) {
    Serial.println("=== ADS1232 Debug Info ===");
    Serial.print("Timestamp: "); Serial.println(info.timestamp);
    Serial.print("Raw Value: "); Serial.print(info.rawValue);
    Serial.print(" | Smoothed: "); Serial.println(info.smoothedValue);
    Serial.print("Tare Offset: "); Serial.println(info.tareOffset);
    Serial.print("Conv Time: "); Serial.print(info.conversionTimeMs, 3);
    Serial.print("ms | SPS: "); Serial.println(info.sps, 2);
    Serial.print("Samples: "); Serial.print(info.samplesInUse);
    Serial.print(" | Valid: "); Serial.print(info.validSamples);
    Serial.print(" | Read Index: "); Serial.println(info.readIndex);

    Serial.print("Flags - OutOfRange: "); Serial.print(info.dataOutOfRange);
    Serial.print(" | SignalTimeout: "); Serial.println(info.signalTimeout);
    Serial.println("==========================");

    lastDebugPrint = now;
  }
}


void aceButtonHandleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  power_off(-1);
  if (b_u8g2Sleep) {
    u8g2.setPowerSave(0);
    b_u8g2Sleep = false;
  }
  if (b_softSleep) {
    Serial.println("Exit Soft Sleep.");
    wakeScaleFromSoftSleep("button soft wake");
  }
  u8g2.setContrast(255);
  b_websocketLowPowerEnabled = false;
  int pin = button->getPin();
  switch (eventType) {
    case AceButton::kEventPressed:
#ifdef BUZZER
      if (GPIO_power_on_with != BATTERY_CHARGING)
        buzzer.beep(1, BUZZER_DURATION);
#endif
      switch (pin) {
        case BUTTON_CIRCLE:
          buttonCircle_Pressed();
          break;
        case BUTTON_SQUARE:
          buttonSquare_Pressed();
          break;
      }
      break;
    case AceButton::kEventDoubleClicked:
      switch (pin) {
        case BUTTON_CIRCLE:
          buttonCircle_DoubleClicked();
          break;
        case BUTTON_SQUARE:
          buttonSquare_DoubleClicked();
          break;
      }
      break;
    case AceButton::kEventLongPressed:
      switch (pin) {
        case BUTTON_CIRCLE:
          buttonCircle_LongPressed();
          break;
        case BUTTON_SQUARE:
          buttonSquare_LongPressed();
          break;
      }
      break;
    case AceButton::kEventReleased:
      switch (pin) {
        case BUTTON_CIRCLE:
          buttonCircle_Released();
          break;
        case BUTTON_SQUARE:
          buttonSquare_Released();
          break;
      }
      break;
  }
}

void scaleTimer() {
  if (stopWatch.isRunning() == false) {
    if (stopWatch.elapsed() == 0) {
      stopWatch.start();
      Serial.println("Timer Start");
    }
    if (stopWatch.elapsed() > 0) {
      stopWatch.reset();
      Serial.println("Timer Reset");
    }
  } else {
    stopWatch.stop();
    Serial.println("Timer Stop");
  }
}

void wakeFromChargingUi(uint8_t buttonPin) {
  if (GPIO_power_on_with != BATTERY_CHARGING && !b_showChargingUI) {
    return;
  }
  GPIO_power_on_with = buttonPin;
  b_showChargingUI = false;
  b_is_charging = false;
  if (!b_ble_enabled) {
    b_ble_enabled = true;
    ble_init();
  }
#if HDS_FEATURE_WIFI
  if (!b_wifiEnabled) {
    wifi_init();
  }
#endif
}

void buttonCircle_Released() {
  Serial.println("O button released");
  onButtonReleased(BUTTON_CIRCLE);
}

void buttonCircle_Pressed() {
  if (b_showChargingUI && i_buttonBootDelay == 0) {
    wakeFromChargingUi(BUTTON_CIRCLE);
  }

  if (b_menu) {
    navigateMenu(1);
  }
  if (b_calibration) {
    i_cal_weight++;
    Serial.print("i_cal_weight = ");
    Serial.println(i_cal_weight);
    if (i_cal_weight > 5)
      i_cal_weight = 0;
  }
  if (!b_calibration) {
    startPressSampling(BUTTON_CIRCLE);
  }
}
void buttonSquare_Released() {
  Serial.println("□ button released");
  if (!b_calibration) {
    onButtonReleased(BUTTON_SQUARE);
  }
}

void buttonSquare_Pressed() {
  if (b_showChargingUI && i_buttonBootDelay == 0) {
    wakeFromChargingUi(BUTTON_SQUARE);
  }
  if (b_menu) {
    selectMenu();
  }
  if (b_calibration) {
    i_button_cal_status++;
    Serial.print("i_button_cal_status:");
    Serial.println(i_button_cal_status);
  }
  if (bleHasLiveClient() && millis() - t_shutdownFailBle < 3000 && !b_menu && millis() - t_menuExitTime > 1000) {
    Serial.println("Going to sleep now by SquarePress");
    b_powerOff = true;
  }
  startPressSampling(BUTTON_SQUARE);
}

void setButtonPressConfig(int button, float min_peak, float max_net,
                         float min_recovery, unsigned long max_press_time,
                         unsigned long min_total_time) {
  Serial.print("Button config updated for ");
  Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
  Serial.print(": min_peak=");
  Serial.print(min_peak);
  Serial.print(", max_net=");
  Serial.print(max_net);
  Serial.print(", min_recovery=");
  Serial.print(min_recovery);
  Serial.println();
}



void buttonCircle_DoubleClicked() {
  Serial.println("O button double clicked");
  const bool bleClientLive = bleHasLiveClient();
  if (!bleClientLive && !b_menu && !b_calibration) {
    Serial.println("Going to sleep now by CircleDoubleClick");
    sendBlePowerOff(1);
#if HDS_FEATURE_WEBSOCKET
    sendWebsocketPowerOff(1);
#endif
    b_powerOff = true;
  } else {
    if (bleClientLive) {
      t_shutdownFailBle = millis();
      b_shutdownFailBle = true;
      Serial.println("BLE connected, not going to sleep.");
    }
    if (b_menu)
      Serial.println("Menu operating, not going to sleep.");
    if (!b_menu) {
      sendBlePowerOff(0);
#if HDS_FEATURE_WEBSOCKET
      sendWebsocketPowerOff(0);
#endif
    }
  }
}

void buttonSquare_DoubleClicked() {
  Serial.println("[] button double clicked");
  const bool bleClientLive = bleHasLiveClient();
  if (!bleClientLive && !b_menu && !b_calibration) {
    Serial.println("Going to sleep now by SquareDoubleClick");
    sendBlePowerOff(2);
#if HDS_FEATURE_WEBSOCKET
    sendWebsocketPowerOff(2);
#endif
    b_powerOff = true;
  } else {
    if (bleClientLive) {
      t_shutdownFailBle = millis();
      b_shutdownFailBle = true;
      Serial.println("BLE connected, not going to sleep.");
    }
    if (b_menu)
      Serial.println("Menu operating, not going to sleep.");
    if (!b_menu) {
      sendBlePowerOff(0);
#if HDS_FEATURE_WEBSOCKET
      sendWebsocketPowerOff(0);
#endif
    }
  }
}

void buttonCircle_LongPressed() {
  if (!b_menu) {
    Serial.println("O button long pressed");
#ifdef BUZZER
    buzzer.beep(1, 200);
#endif
    if (GPIO_power_on_with == BATTERY_CHARGING) {
      wakeFromChargingUi(BUTTON_CIRCLE);
    }
  }
}

void buttonSquare_LongPressed() {
  if (!b_menu) {
    Serial.println("[] button long pressed");
#ifdef BUZZER
    buzzer.beep(1, 200);
#endif
    if (GPIO_power_on_with == BATTERY_CHARGING) {
      wakeFromChargingUi(BUTTON_SQUARE);
    }
  }
}

#if HDS_ENABLE_GRINDER
bool handleGrinderMenuChord() {
  static bool handled = false;
  static uint32_t pressedAt = 0;
  const bool bothPressed = digitalRead(BUTTON_CIRCLE) == LOW && digitalRead(BUTTON_SQUARE) == LOW;
  if (!bothPressed) {
    handled = false;
    pressedAt = 0;
    return false;
  }
  if (handled) {
    return true;
  }
  if (!grinderSettings.enabled || b_menu || b_calibration || GPIO_power_on_with == BATTERY_CHARGING) {
    return false;
  }
  const uint32_t now = millis();
  if (pressedAt == 0) {
    pressedAt = now;
    return true;
  }
  if (now - pressedAt < GRINDER_MENU_CHORD_HOLD_MS) {
    return true;
  }
  b_menu = true;
  b_grinderMenuDirectEntry = true;
  b_buttonChordSuppressUntilRelease = true;
  currentMenu = grinderMenu;
  currentMenuSize = getMenuSize(grinderMenu);
  currentIndex = 0;
  currentSelection = currentMenu[currentIndex];
  grinderPauseForMenu();
  handled = true;
  Serial.println("[grinder] menu chord");
  return true;
}
#endif


void button_init() {
  pinMode(BUTTON_CIRCLE, INPUT_PULLUP);
  pinMode(BUTTON_SQUARE, INPUT_PULLUP);
  buttonCircle.init(BUTTON_CIRCLE);
  buttonSquare.init(BUTTON_SQUARE);
  config1.setEventHandler(aceButtonHandleEvent);
  config1.setFeature(ButtonConfig::kFeatureClick);
  config1.setFeature(ButtonConfig::kFeatureDoubleClick);
  config1.setFeature(ButtonConfig::kFeatureLongPress);
  config1.setClickDelay(CLICK_DELAY);
  config1.setDoubleClickDelay(DOUBLECLICK_DELAY);
  config1.setLongPressDelay(LONGPRESS_DELAY);
}

#if HDS_FEATURE_WIFI
void _wifi_init(void *args) {
  b_wifiEnabled = true;
  setupWifi();
#if HDS_FEATURE_WEBSERVER
  startWebServer();
#endif
#if HDS_FEATURE_ELEGANT_OTA
  wifiOta();
#endif
#if HDS_FEATURE_WEBSOCKET
  setupWebsocketEvents();
#endif
  vTaskDelete(NULL);
}
void wifi_init() {
  if (!b_wifiOnBoot) {
    return;
  }
  xTaskCreate(_wifi_init, "Wifi Init Task", configMINIMAL_STACK_SIZE + 2048, NULL, 0, NULL);
}
#endif

MyUsbCallbacks usbCallbacks;

const char *resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "ext";
    case ESP_RST_SW:        return "sw";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
  }
}

#if HDS_ENABLE_GRINDER
void beforeDeepSleepFlush() {
  grinderFlushSettingsIfDirty();
}
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;
  {
    esp_reset_reason_t r = esp_reset_reason();
    g_resetReasonCode = (uint8_t)r;
    Serial.printf("[boot] reset_reason=%s (%u)\n", resetReasonStr(r), (unsigned)g_resetReasonCode);
#if HDS_FEATURE_PULL_OTA
    hdsOtaRollbackBegin(r);
#endif
  }
  if (!storageInit()) {
    Serial.println("NVS settings init failed!");
    while (1) {
      delay(1000);
    }
  }
#if HDS_ENABLE_GRINDER
  grinderLoadSettings();
#endif

  b_quickBoot = storageGetBool(KEY_QUICK_BOOT, false);
  i_buttonBootDelay = b_quickBoot ? 0 : 500;

  Serial.println("NVS settings init success");

  usbCallbacks.setStableOutputThreshold = setStableOutputThreshold;
  usbCallbacks.setTrackingThreshold = setTrackingThreshold;
  usbCallbacks.setTrackingUpdateInterval = setTrackingUpdateInterval;
  usbCallbacks.buttonSquare_Pressed = buttonSquare_Pressed;
  usbCallbacks.buttonCircle_Pressed = buttonCircle_Pressed;
  usbCallbacks.toggleTimer = scaleTimer;
#ifdef ESP32
  releaseWakePinsFromRtcMode();
#endif
  button_init();
  pinMode(BATTERY_CHARGING, INPUT_PULLUP);
#if defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  pinMode(USB_DET, INPUT_PULLUP);
  pinMode(OLED_CS, OUTPUT);
  pinMode(OLED_DC, OUTPUT);
  pinMode(SCALE_PDWN, OUTPUT);
  pinMode(SCALE_SCLK, OUTPUT);
  pinMode(ACC_PWR_CTRL, OUTPUT);
  pinMode(PWR_CTRL, OUTPUT);
#endif
  print_wakeup_reason();
  Serial.println("GPIO_power_on_with = " + String(GPIO_power_on_with));

  if (GPIO_power_on_with == BATTERY_CHARGING)
    b_is_charging = true;
  if (GPIO_power_on_with == BUTTON_CIRCLE)
    b_ble_enabled = true;
  else {
    b_ble_enabled = false;
  }
  if (GPIO_power_on_with == -1) {
    b_ble_enabled = true;
  }
  while (true && GPIO_power_on_with > 0) {
    if (i_buttonBootDelay == 0){
      Serial.println("Quick boot. Powering on...");
      break;
    }

    if (digitalRead(GPIO_power_on_with) == LOW) {
      if (!b_button_pressed) {
        t_power_on_button = millis();
        b_button_pressed = true;
      }

      if (millis() - t_power_on_button >= i_buttonBootDelay) {
        Serial.println("Button held for 0.5 second. Powering on...");
        break;
      }
    } else {
      Serial.println("Button released before 0.5 second.");
      Serial.println("Going to sleep now.");
      esp32_sleep();
      break;
      b_button_pressed = false;
    }
  }
  pinMode(PWR_CTRL, OUTPUT);
  digitalWrite(PWR_CTRL, HIGH);
  pinMode(ACC_PWR_CTRL, OUTPUT);
  digitalWrite(ACC_PWR_CTRL, HIGH);
  pinMode(OLED_SDIN, OUTPUT);
  digitalWrite(OLED_SDIN, LOW);
  pinMode(OLED_SCLK, OUTPUT);
  digitalWrite(OLED_SCLK, LOW);
  pinMode(OLED_DC, OUTPUT);
  digitalWrite(OLED_DC, LOW);
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, HIGH);
  pinMode(OLED_CS, OUTPUT);
  digitalWrite(OLED_CS, HIGH);
  pinMode(SCALE_SCLK, OUTPUT);
  digitalWrite(SCALE_SCLK, LOW);
  pinMode(SCALE_PDWN, OUTPUT);
  digitalWrite(SCALE_PDWN, HIGH);
  pinMode(SCALE_DOUT, INPUT);
  pinMode(SCALE2_SCLK, OUTPUT);
  digitalWrite(SCALE2_SCLK, LOW);
  pinMode(SCALE2_PDWN, OUTPUT);
  digitalWrite(SCALE2_PDWN, HIGH);
  pinMode(SCALE2_DOUT, INPUT);
  pinMode(I2C_SCL, INPUT_PULLUP);
  pinMode(I2C_SDA, INPUT_PULLUP);
  gpio_hold_dis((gpio_num_t)OLED_SDIN);
  gpio_hold_dis((gpio_num_t)OLED_SCLK);
  gpio_hold_dis((gpio_num_t)OLED_DC);
  gpio_hold_dis((gpio_num_t)OLED_RST);
  gpio_hold_dis((gpio_num_t)OLED_CS);
  gpio_hold_dis((gpio_num_t)SCALE_SCLK);
  gpio_hold_dis((gpio_num_t)SCALE_PDWN);
  gpio_hold_dis((gpio_num_t)SCALE_DOUT);
  gpio_hold_dis((gpio_num_t)SCALE2_SCLK);
  gpio_hold_dis((gpio_num_t)SCALE2_PDWN);
  gpio_hold_dis((gpio_num_t)SCALE2_DOUT);
  gpio_hold_dis((gpio_num_t)I2C_SCL);
  gpio_hold_dis((gpio_num_t)I2C_SDA);
  gpio_hold_dis((gpio_num_t)PWR_CTRL);

#if defined(V7_3) || defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  gpio_hold_dis((gpio_num_t)ACC_PWR_CTRL);
  Serial.println("ACC_PWR_CTRL = HIGH");
#endif
  gpio_deep_sleep_hold_dis();
#ifdef ESP32
  Wire.begin(I2C_SDA, I2C_SCL);
#endif
#ifdef HW_SPI
  SPI.begin(OLED_SCLK, -1, OLED_SDIN, OLED_CS);
#endif
#ifdef ADS1115ADC
  ADS_init();
#endif
  delay(50);
  b_requireHeartBeat = storageGetBool(KEY_HEARTBEAT, true);
  if (b_ble_enabled) {
    ble_init();
  }
  Serial.println("Begin!");
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
  ACC_init();
  Serial.println("ACC_init complete");
  if (b_gyroEnabled) {
#ifdef GYROFACEUP
    if (gyro_z() < 8) {
      Serial.print("gyro_z:");
      Serial.println(gyro_z());
      shut_down_now_accidentTouch();
    }
#endif
#ifdef GYROFACEDOWN
    if (gyro_z() > -8) {
      Serial.print("gyro_z:");
      Serial.println(gyro_z());
      shut_down_now_accidentTouch();
    }
#endif
  }
#endif
  analogReadResolution(ADC_BIT);
#ifdef BUZZER
  pinMode(BUZZER, OUTPUT);
  b_beep = storageGetInt(KEY_BEEP, 1);

  if (GPIO_power_on_with != BATTERY_CHARGING) {
    buzzer.beep(1, BUZZER_DURATION);
  }
#endif
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(255);
  u8g2.setFont(FONT_M);
  power_off(15);
  b_screenFlipped = storageGetBool(KEY_SCREEN_FLIP, false);
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_logisoso22_tf);
    u8g2.drawStr(AC("Half"), 26, "Half");
    u8g2.drawBox(4, LCDHeight / 2, LCDWidth - 4 * 2, 2);
    u8g2.drawStr(AC("Decent"), LCDHeight - 2, "Decent");
  } while (u8g2.nextPage());
  unsigned long stabilizingtime = 500;
  scale.begin();
  scale.setSamplesInUse(1);
  scale.start(stabilizingtime, false);
  resetAdcRecoveryState();
  scale.setCalFactor(f_calibration_value);

  scale.setDebugCallback(adsDebugCallback);


  stopWatch.setResolution(StopWatch::SECONDS);
  stopWatch.start();
  stopWatch.reset();
  INPUTCOFFEEPOUROVER = storageGetFloat(KEY_POUROVER, 16.0f);
  INPUTCOFFEEESPRESSO = storageGetFloat(KEY_ESPRESSO, 18.0f);
  f_batteryCalibrationFactor = storageGetFloat(KEY_BAT_CAL, storageBatteryCalibrationDefault());
  b_mode = storageGetInt(KEY_MODE, 0);
  f_maxDriftCompensation = storageGetFloat(KEY_DRIFT_MAX, 0.05f);

  if (isnan(INPUTCOFFEEPOUROVER)) {
    INPUTCOFFEEPOUROVER = 16.0;
    storagePutFloat(KEY_POUROVER, INPUTCOFFEEPOUROVER);
  }
  if (isnan(INPUTCOFFEEESPRESSO)) {
    INPUTCOFFEEESPRESSO = 18.0;
    storagePutFloat(KEY_ESPRESSO, INPUTCOFFEEESPRESSO);
  }
  if (isnan(f_maxDriftCompensation)) {
    f_maxDriftCompensation = 0.05;
    storagePutFloat(KEY_DRIFT_MAX, f_maxDriftCompensation);
  }
#ifdef V7_2
  if (isnan(f_batteryCalibrationFactor) || f_batteryCalibrationFactor < 1.4 || f_batteryCalibrationFactor > 1.8) {
    f_batteryCalibrationFactor = 1.66;
    storagePutFloat(KEY_BAT_CAL, f_batteryCalibrationFactor);
  }
#else
  if (isnan(f_batteryCalibrationFactor) || f_batteryCalibrationFactor < 0.9 || f_batteryCalibrationFactor > 1.3) {
    f_batteryCalibrationFactor = 1.06;
    storagePutFloat(KEY_BAT_CAL, f_batteryCalibrationFactor);
  }
#endif
#ifdef BUZZER
  if (b_beep != 0 && b_beep != 1) {
    b_beep = 1;
    storagePutInt(KEY_BEEP, b_beep);
  }
#endif

  b_timeOnTop = storageGetBool(KEY_TIME_ON_TOP, false);
  b_btnFuncWhileConnected = storageGetBool(KEY_BTN_CONN, false);
  b_autoSleep = storageGetBool(KEY_AUTO_SLEEP, true);
  if (b_mode > 1) {
    b_mode = 0;
    storagePutInt(KEY_MODE, b_mode);
  }
  if (b_mode < 0) {
    b_mode = 0;
    storagePutInt(KEY_MODE, b_mode);
  }

  f_calibration_value = storageGetFloat(KEY_CAL1, CALIBRATION_VALUE_DEFAULT);
  if (!isValidCalibrationValue(f_calibration_value)) {
    float storedCalibrationValue = f_calibration_value;
    CalibrationRejectReason bootCalibrationReason = CAL_REJECT_FACTOR_SIGN;
    if (!isfinite(storedCalibrationValue) ||
        fabsf(storedCalibrationValue) < CALIBRATION_VALUE_MIN_ABS) {
      bootCalibrationReason = CAL_REJECT_FACTOR_NEAR_ZERO;
    } else if (fabsf(storedCalibrationValue) > CALIBRATION_VALUE_MAX_ABS) {
      bootCalibrationReason = CAL_REJECT_FACTOR_TOO_LARGE;
    }
    b_calibrationInvalid = true;
    snprintf(c_calibrationStatus, sizeof(c_calibrationStatus), "%s",
             calibrationRejectReasonText(bootCalibrationReason));
    Serial.print(F("Invalid stored calibration value: "));
    Serial.print(storedCalibrationValue, 6);
    Serial.print(F(" reason="));
    Serial.println(c_calibrationStatus);
    f_calibration_value = CALIBRATION_VALUE_DEFAULT;
    Serial.println(F("Using temporary default calibration; NVS not overwritten."));
  } else {
    b_calibrationInvalid = false;
    snprintf(c_calibrationStatus, sizeof(c_calibrationStatus), "%s",
             calibrationRejectReasonText(CAL_REJECT_NONE));
  }
  scale.setCalFactor(f_calibration_value);

#ifdef DEBUG
  if (digitalRead(BUTTON_DEBUG) == LOW && digitalRead(BUTTON_CIRCLE) == HIGH)
    b_debug = true;
  if (digitalRead(BUTTON_DEBUG) == LOW && digitalRead(BUTTON_CIRCLE) == LOW)
    b_debug_battery = true;
#endif  //DEBUG

#ifdef CAL
  if (digitalRead(BUTTON_CIRCLE) == HIGH && digitalRead(BUTTON_SQUARE) == LOW) {
    b_calibration = true;
    i_button_cal_status = 1;
    calibration(0);
  }
#endif
#if HDS_FEATURE_WIFI
  b_wifiOnBoot = storageGetBool(KEY_WIFI_BOOT, false);
#endif
#if HDS_ENABLE_GRINDER
  if (grinderSettings.enabled && !b_wifiOnBoot) {
    b_wifiOnBoot = true;
  }
  grinderRuntimeBegin();
#endif
#if HDS_FEATURE_PULL_OTA
  bool b_pendingOtaLittleFs = pullOtaHasPendingLittleFs();
#else
  bool b_pendingOtaLittleFs = false;
#endif
#if HDS_FEATURE_WIFI
  if (b_wifiOnBoot && GPIO_power_on_with != BATTERY_CHARGING && !b_pendingOtaLittleFs) {
    wifi_init();
  }
#endif
  if (digitalRead(BUTTON_CIRCLE) == LOW && digitalRead(BUTTON_SQUARE) == LOW) {
    b_menu = true;
    b_buttonChordSuppressUntilRelease = true;
#if HDS_ENABLE_GRINDER
    b_grinderMenuDirectEntry = false;
    grinderPauseForMenu();
#endif
    refreshOLED((char *)"HDS Setup", FONT_EXTRACTION);
    delay(1000);
  }

#if DEBUG
  Serial.print("digitalRead(BUTTON_CIRCLE):");
  Serial.print(digitalRead(BUTTON_CIRCLE));
  Serial.print("\tdigitalRead(BUTTON_CIRCLE):");
  Serial.println(digitalRead(BUTTON_CIRCLE));
#endif

  {
#ifdef WELCOME
    String welcome = storageGetString(KEY_WELCOME, String(WELCOME1));
    welcome.trim();
#else
    String welcome = "welcome";
#endif
    Serial.print("Welcome: ");
    if (welcome.length() == 127)
      Serial.print(WELCOME1);
    else
      Serial.print(welcome);
    Serial.print("\t");
    Serial.print(WELCOME2);
    Serial.print("\t");
    Serial.println(WELCOME3);
  }
  Serial.print("Info: ");
  Serial.print(LINE1);
  Serial.print("\t");
  Serial.print(LINE2);
  Serial.print("\t");
  Serial.print(LINE3);
  Serial.print("\t");
  Serial.println(GIT_REV);
  Serial.print("\tCal_Val: ");
  Serial.print(f_calibration_value);
  Serial.print("\tHB_DET: ");
  Serial.print(b_requireHeartBeat);
  Serial.print("\tTime_on_top: ");
  Serial.print(b_timeOnTop);
  Serial.print("\tButton_Func_While_Connected: ");
  Serial.print(b_btnFuncWhileConnected);
  Serial.println("");


  Serial.println("Button:\tSQARE\tCIRCLE");
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(BUTTON_SQUARE);
  Serial.print("\t");
  Serial.println(BUTTON_CIRCLE);
#ifdef ADS1232ADC
#ifdef BUZZER
  Serial.println("Pin:\tI2C_SDA\tI2C_SCK ADC_DOUT ADC_SCLK\tADC_PWDN\tBUZZER");
#else
  Serial.println("Pin:\tI2C_SDA\tI2C_SCK ADC_DOUT ADC_SCLK\tADC_PWDN");
#endif
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(I2C_SDA);
  Serial.print("\t");
  Serial.print(I2C_SCL);
  Serial.print("\t ");
  Serial.print(SCALE_DOUT);
  Serial.print("\t ");
  Serial.print(SCALE_SCLK);
  Serial.print("\t");
  Serial.print(SCALE_PDWN);
#ifdef BUZZER
  Serial.print("\t");
  Serial.print(BUZZER);
#endif
  Serial.println("");

#endif
#ifdef HX711ADC
  Serial.println("Button:\tI2C_SDA\tI2C_SCK\t711SDA\t711SCK\tBUZZER");
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(I2C_SDA);
  Serial.print("\t");
  Serial.print(I2C_SCL);
  Serial.print("\t");
  Serial.print(HX711_SDA);
  Serial.print("\t");
  Serial.print(HX711_SCL);
  Serial.print("\t");
  Serial.println(BUZZER);
#endif

  Serial.println("Setup complete...");
  t_bootTare = millis();
  b_bootTare = true;
  updateBattery(BATTERY_PIN);
#if HDS_FEATURE_PULL_OTA
  if (b_pendingOtaLittleFs) {
    if (!pullOtaResumePendingLittleFs()) {
      hdsOtaRollback("LittleFS update");
    }
  } else {
    hdsOtaRollbackMarkValid();
  }
#endif
}

void updateAdaptiveTracking(float current_weight) {
  unsigned long current_time = millis();

  if (!b_tracking_enabled) {
    return;
  }

  float weight_diff = current_weight - f_tracking_target;

  if (fabs(weight_diff) <= TRACKING_THRESHOLD) {
    i_stable_count++;

    if (i_stable_count >= 3) {
      float adjustment = weight_diff * 0.1;

      if (fabs(adjustment) > MAX_TRACKING_ADJUSTMENT) {
        adjustment = (adjustment > 0) ? MAX_TRACKING_ADJUSTMENT : -MAX_TRACKING_ADJUSTMENT;
      }

      f_tracking_target += adjustment;
   }

  } else {
    i_stable_count = 0;
    b_tracking_active = false;

    if (fabs(weight_diff) > TRACKING_THRESHOLD * 2) {
      if (verifyWeightStability(current_weight)) {
        f_tracking_target = current_weight;
        b_tracking_active = true;
        if (b_weight_in_serial) {
          Serial.print("New weight target set: ");
          Serial.println(f_tracking_target, 4);
        }
      }
    }
  }

  if (i_stable_count >= i_STABLE_COUNT_THRESHOLD) {
    if (current_time - t_last_tracking_update >= TRACKING_UPDATE_INTERVAL) {
      performTrackingAdjustment(current_weight);
    }
  }
}

void performTrackingAdjustment(float current_weight) {
  float old_offset = f_tracking_offset;

  float calculated_offset = current_weight - f_tracking_target;

  f_tracking_offset = f_tracking_offset * 0.8 + calculated_offset * 0.2;

  if (!b_tracking_active) {
    b_tracking_active = true;
  }

  if (b_weight_in_serial) {
    Serial.print("Tracking adjustment: Offset ");
    Serial.print(old_offset, 4);
    Serial.print("g -> ");
    Serial.print(f_tracking_offset, 4);
    Serial.print("g | Target: ");
    Serial.print(f_tracking_target, 4);
    Serial.print("g | Raw: ");
    Serial.print(current_weight, 4);
    Serial.println("g");
  }

  i_stable_count = i_STABLE_COUNT_THRESHOLD - 2;
  t_last_tracking_update = millis();
}

bool verifyWeightStability(float current_weight) {
  static float last_verified_weight = 0.0;
  static int verification_count = 0;

  if (fabs(current_weight - last_verified_weight) <= TRACKING_THRESHOLD) {
    verification_count++;
  } else {
    verification_count = 0;
  }

  last_verified_weight = current_weight;

  return (verification_count >= 3);
}

float applyTrackingCompensation(float raw_weight) {
  if (b_tracking_active && b_tracking_enabled) {
    return raw_weight - f_tracking_offset;
  }
  return raw_weight;
}

float applyStableOutput(float current_value) {
  if (!b_stable_output_enabled) {
    return current_value;
  }

  float change = fabs(current_value - f_previous_stable_value);

  if (change >= STABLE_OUTPUT_THRESHOLD) {
    f_previous_stable_value = current_value;
    t_last_stable_change = millis();

    if (b_weight_in_serial) {
      Serial.print("Output updated: ");
      Serial.print(current_value, 4);
      Serial.print("g (Change: ");
      Serial.print(change, 4);
      Serial.println("g)");
    }
  }

  return f_previous_stable_value;
}

void pureScale() {
  static bool b_newDataReady = 0;
  static float f_last_displayed = 0.0;
  static unsigned long t_lastScaleData = 0;
  static unsigned long t_lastScaleRecovery = 0;
  static unsigned long t_zeroDisplayMismatch = 0;
  static int f_similar_diff_count = 0;
  static float f_last_diff = 0.0;

  if (t_lastScaleData == 0) {
    t_lastScaleData = millis();
  }

  if (scale.update()) {
    b_newDataReady = true;
    t_lastScaleData = millis();
    resetAdcRecoveryState();
  } else if (scale.getSignalTimeoutFlag() &&
             millis() - t_lastScaleData > 1500 &&
             millis() - t_lastScaleRecovery > 5000) {
    Serial.println("Scale ADC timeout. Power cycling ADC.");
    b_adc_recovery_active = true;
    if (i_adc_recovery_count < 255) {
      i_adc_recovery_count++;
    }
    scale.powerDown();
    delay(5);
    scale.powerUp();
    if (refreshScaleDatasetAfterDiscontinuity("ADC recovery") &&
        tareScaleWhenAdcReady("ADC recovery tare")) {
      resetScaleOutputAfterAdcDiscontinuity();
    }
    t_lastScaleRecovery = millis();
    t_lastScaleData = millis();
  }

  if (b_newDataReady) {
    float raw_weight = scale.getData();
    updatePressSampling();
    f_current_raw_value = raw_weight;

    float current_diff = raw_weight - f_displayedValue - f_driftCompensation;

    if (fabs(current_diff) > 0.01 && fabs(current_diff) < f_maxDriftCompensation) {
      if ((f_last_diff * current_diff) > 0) {
        f_similar_diff_count++;

        if (f_similar_diff_count >= 3) {
          f_driftCompensation += current_diff * 0.3;

          if (fabs(f_driftCompensation) > 2.0) {
            f_driftCompensation = (f_driftCompensation > 0) ? 2.0 : -2.0;
          }

          if (b_weight_in_serial) {
            Serial.print("TEMP-DRIFT-COMP: diff=");
            Serial.print(current_diff, 4);
            Serial.print("g, total_comp=");
            Serial.print(f_driftCompensation, 4);
            Serial.print("g, count=");
            Serial.println(f_similar_diff_count);
          }

          f_similar_diff_count = 2;
        }
      } else {
        f_similar_diff_count = 1;
      }

      f_last_diff = current_diff;
    } else {
      f_similar_diff_count = 0;
      f_last_diff = 0.0;
    }

    float temperature_compensated = raw_weight - f_driftCompensation;

    float tracking_compensated = applyTrackingCompensation(temperature_compensated);
#if HDS_ENABLE_GRINDER
    f_grinder_fast_weight = tracking_compensated;
    grinderFastWeightSequence++;
    if (grinderFastWeightSequence == 0) {
      grinderFastWeightSequence = 1;
    }
    grinderRuntimeFreshWeightTick(f_grinder_fast_weight, grinderFastWeightSequence);
#endif
    float stable_output = applyStableOutput(tracking_compensated);

    if (stable_output >= -0.14 && stable_output <= 0.14) {
      f_displayedValue = 0.0;
    } else {
      f_displayedValue = stable_output;
    }
    updateAdaptiveTracking(tracking_compensated);

    if (!b_bootTare && !b_weight_quick_zero &&
        fabs(f_displayedValue) <= 0.14 &&
        fabs(raw_weight) > ZERO_DISPLAY_MISMATCH_THRESHOLD) {
      if (t_zeroDisplayMismatch == 0) {
        t_zeroDisplayMismatch = millis();
      } else if (millis() - t_zeroDisplayMismatch > ZERO_DISPLAY_MISMATCH_TIMEOUT) {
        Serial.println("Display held at zero while raw weight moved. Resetting output filters.");
        resetTracking();
        resetStableOutput();
        f_driftCompensation = 0.0;
        f_displayedValue = raw_weight;
        t_zeroDisplayMismatch = 0;
      }
    } else {
      t_zeroDisplayMismatch = 0;
    }

    formatFloatSafe(c_weight, sizeof(c_weight), f_displayedValue, i_decimal_precision);
    if (b_weight_in_serial == true) {
      unsigned long current_time = millis();
      if (current_time - t_last_status_display >= STATUS_DISPLAY_INTERVAL) {
        Serial.println("=== Temperature Drift Status ===");
        Serial.print("Raw: ");
        Serial.print(raw_weight, 4);
        Serial.print("g | TempComp: ");
        Serial.print(f_driftCompensation, 4);
        Serial.print("g | AfterTempComp: ");
        Serial.print(temperature_compensated, 4);
        Serial.println("g");

        Serial.print("Displayed: ");
        Serial.print(f_displayedValue, 4);
        Serial.print("g | Raw-Display Diff: ");
        Serial.print(raw_weight - f_displayedValue, 4);
        Serial.println("g");

        displayEnhancedStatus(temperature_compensated, tracking_compensated, stable_output);
        t_last_status_display = current_time;
      }
    }

    b_newDataReady = false;
  }

  if (!b_bootFreshTarePending && scale.getTareStatus()) {
    t_tareStatus = millis();
    b_weight_quick_zero = false;
    resetTracking();
    resetStableOutput();
    f_driftCompensation = 0.0;
    f_displayedValue = 0.0;
#if HDS_ENABLE_GRINDER
    f_grinder_fast_weight = 0.0f;
    grinderRuntimeNotifyTareComplete();
#endif
    if (b_weight_in_serial) {
      Serial.println("TARE: Temperature drift compensation reset");
    }
  }
  if (b_weight_quick_zero && millis() - t_quickZeroStart > QUICK_ZERO_HOLD_TIMEOUT) {
    Serial.println("Quick zero timeout. Releasing display zero hold.");
    b_weight_quick_zero = false;
    resetTracking();
    resetStableOutput();
    f_driftCompensation = 0.0;
#if HDS_ENABLE_GRINDER
    f_grinder_fast_weight = 0.0f;
#endif
  }


  if (b_weight_quick_zero || b_bootTare) {
    f_displayedValue = 0.0;
    f_driftCompensation = 0.0;
  }
}

float getTemperatureDriftCompensation() {
  return f_driftCompensation;
}

void adjustTemperatureDriftCompensation(float amount) {
  f_driftCompensation += amount;
  Serial.print("Manual temp-comp adjust: ");
  Serial.print(amount, 4);
  Serial.print("g, total: ");
  Serial.println(f_driftCompensation, 4);
}

void resetTracking() {
  f_tracking_offset = 0.0;
  f_tracking_target = 0.0;
  i_stable_count = 0;
  b_tracking_active = false;
  t_last_tracking_update = millis();
  if (b_weight_in_serial) {
    Serial.println("Tracking system reset");
  }
}

void resetStableOutput() {
  f_previous_stable_value = 0.0;
  t_last_stable_change = millis();
  if (b_weight_in_serial) {
    Serial.println("Stable output reset");
  }
}

bool wakeScaleFromSoftSleep(const char *context) {
  digitalWrite(PWR_CTRL, HIGH);
  digitalWrite(ACC_PWR_CTRL, HIGH);
  delay(5);
  scale.powerUp();
  u8g2.setPowerSave(0);
  b_softSleep = false;
  b_u8g2Sleep = false;
  if (refreshScaleDatasetAfterDiscontinuity(context)) {
    resetScaleOutputAfterAdcDiscontinuity();
    return true;
  }
  return false;
}

bool refreshScaleDatasetAfterDiscontinuity(const char *context) {
  if (scale.refreshDataSet()) {
    resetAdcRecoveryState();
    Serial.print(context);
    Serial.println(" dataset refreshed");
    return true;
  }

  b_adc_recovery_active = true;
  if (i_adc_recovery_count < 255) {
    i_adc_recovery_count++;
  }
  Serial.print(context);
  Serial.println(" dataset refresh failed");
  return false;
}

void resetScaleOutputAfterAdcDiscontinuity() {
  resetTracking();
  resetStableOutput();
  f_driftCompensation = 0.0;
  f_displayedValue = 0.0;
#if HDS_ENABLE_GRINDER
  f_grinder_fast_weight = 0.0f;
#endif
  formatFloatSafe(c_weight, sizeof(c_weight), f_displayedValue,
                  i_decimal_precision);
}

void holdScaleOutputAtZero() {
  f_driftCompensation = 0.0;
  f_displayedValue = 0.0;
  formatFloatSafe(c_weight, sizeof(c_weight), f_displayedValue,
                  i_decimal_precision);
}

bool isBootFreshTareInputSettled() {
  if (b_menu || b_calibration || GPIO_power_on_with == BATTERY_CHARGING) {
    return false;
  }
  if (digitalRead(BUTTON_CIRCLE) == LOW || digitalRead(BUTTON_SQUARE) == LOW) {
    return false;
  }
  if (t_menuExitTime > 0 &&
      millis() - t_menuExitTime <= BOOT_FRESH_TARE_INPUT_SETTLE) {
    return false;
  }
  return true;
}

void restoreBootFreshTareSamples() {
  uint8_t samplesInUse = i_bootFreshTareSamplesInUse > 0
                           ? i_bootFreshTareSamplesInUse
                           : 1;
  if (scale.getSamplesInUse() != samplesInUse) {
    scale.setSamplesInUse(samplesInUse);
  }
  i_bootFreshTareSamplesInUse = samplesInUse;
}

bool startBootFreshTare() {
  if (b_bootFreshTarePending) {
    return true;
  }

  int currentSamplesInUse = scale.getSamplesInUse();
  i_bootFreshTareSamplesInUse = currentSamplesInUse > 0
                                  ? (uint8_t)currentSamplesInUse
                                  : 1;
  if (scale.getSamplesInUse() != BOOT_FRESH_TARE_SAMPLES) {
    scale.setSamplesInUse(BOOT_FRESH_TARE_SAMPLES);
  }
  scale.tareFreshNoDelay();
  t_bootFreshTare = millis();
  b_bootFreshTarePending = true;
  holdScaleOutputAtZero();
  Serial.println("Boot fresh tare started");
  return true;
}

bool processBootFreshTare() {
  holdScaleOutputAtZero();

  if (!b_bootFreshTarePending) {
    if (!isBootFreshTareInputSettled()) {
      t_bootTare = millis();
      return false;
    }
    if (millis() - t_bootTare <= (unsigned long)i_bootTareDelay) {
      return false;
    }
    return startBootFreshTare();
  }

  if (scale.getTareStatus()) {
    b_bootFreshTarePending = false;
    restoreBootFreshTareSamples();
    if (!refreshScaleDatasetAfterDiscontinuity("boot tare restore")) {
      t_bootTare = millis();
      resetScaleOutputAfterAdcDiscontinuity();
      Serial.println("Boot fresh tare restore failed");
      return false;
    }
    resetScaleOutputAfterAdcDiscontinuity();
    b_bootTare = false;
    t_bootFreshTare = 0;
    Serial.println("Boot fresh tare complete");
    return true;
  }

  if (millis() - t_bootFreshTare > BOOT_FRESH_TARE_TIMEOUT) {
    b_bootFreshTarePending = false;
    restoreBootFreshTareSamples();
    resetScaleOutputAfterAdcDiscontinuity();
    t_bootTare = millis();
    t_bootFreshTare = 0;
    Serial.println("Boot fresh tare timeout");
  }

  return false;
}

bool tareScaleWhenAdcReady(const char *context, bool userRequested) {
  ADS1232DebugInfo info = scale.getDebugInfo();
  if (info.samplesInUse > 0 && info.validSamples < info.samplesInUse) {
    if (!refreshScaleDatasetAfterDiscontinuity(context)) {
      return false;
    }
  }
#if HDS_ENABLE_GRINDER
  grinderRuntimeNotifyTareRequested(userRequested);
#endif
  scale.tareNoDelay();
  return true;
}

void consumeScaleTareStatus() {
  scale.getTareStatus();
}

void clearPendingAutomaticTareState() {
  if (b_bootFreshTarePending) {
    restoreBootFreshTareSamples();
  }
  b_bootFreshTarePending = false;
  b_bootTare = false;
  b_weight_quick_zero = false;
  b_tareByButton = false;
  b_tareByBle = false;
  t_bootFreshTare = 0;
  t_quickZeroStart = 0;
  consumeRemoteTareRequests();
}

bool setScaleSamplesInUseWhenReady(uint8_t samplesInUse, const char *context) {
#if HDS_ENABLE_GRINDER
  if (grinderRuntimeLocksScaleSampling()) {
    Serial.print("Samples in use locked by grinder: ");
    Serial.println(context);
    return false;
  }
#endif
  scale.setSamplesInUse(samplesInUse);
  if (!refreshScaleDatasetAfterDiscontinuity(context)) {
    return false;
  }
  resetScaleOutputAfterAdcDiscontinuity();
  return true;
}

void setStableOutputEnabled(bool enabled) {
  b_stable_output_enabled = enabled;
  if (!enabled) {
    resetStableOutput();
  }
  Serial.print("Stable output ");
  Serial.println(enabled ? "enabled" : "disabled");
}

void setStableOutputThreshold(float threshold) {
  STABLE_OUTPUT_THRESHOLD = threshold;
  Serial.print("Stable threshold set to: ");
  Serial.println(threshold, 4);
}

void setTrackingThreshold(float threshold) {
  TRACKING_THRESHOLD = threshold;
  Serial.print("Tracking threshold set to: ");
  Serial.println(threshold, 4);
}

void setTrackingUpdateInterval(float interval) {
  TRACKING_UPDATE_INTERVAL = interval;
  Serial.print("Tracking update interval set to: ");
  Serial.println(interval, 4);
}


void setTrackingEnabled(bool enabled) {
  b_tracking_enabled = enabled;
  if (!enabled) {
    resetTracking();
  }
  Serial.print("Tracking system ");
  Serial.println(enabled ? "enabled" : "disabled");
}

void displayEnhancedStatus(float raw_weight, float compensated_weight, float stable_weight) {
  Serial.println("=== Enhanced Scale Status ===");
  Serial.print("Raw Input: ");
  Serial.print(raw_weight, 4);
  Serial.print("g | Compensated: ");
  Serial.print(compensated_weight, 4);
  Serial.print("g | Stable Output: ");
  Serial.print(stable_weight, 4);
  Serial.println("g");

  Serial.print("Stable Output: ");
  Serial.print(b_stable_output_enabled ? "ON" : "OFF");
  Serial.print(" | Threshold: ±");
  Serial.print(STABLE_OUTPUT_THRESHOLD, 4);
  Serial.println("g");

  Serial.print("Last Stable Change: ");
  Serial.print((millis() - t_last_stable_change) / 1000);
  Serial.println("s ago");

  Serial.print("Tracking System: ");
  Serial.print(b_tracking_enabled ? "ON" : "OFF");
  Serial.print(" | Active: ");
  Serial.println(b_tracking_active ? "YES" : "NO");

  Serial.print("Tracking Offset: ");
  Serial.print(f_tracking_offset, 4);
  Serial.print("g | Target: ");
  Serial.print(f_tracking_target, 4);
  Serial.println("g");

  Serial.print("Stable Count: ");
  Serial.print(i_stable_count);
  Serial.print("/");
  Serial.println(i_STABLE_COUNT_THRESHOLD);

  Serial.println("=============================");
}

float getTrackingOffset() {
  return f_tracking_offset;
}

float getStableOutputValue() {
  return f_previous_stable_value;
}

void setManualTrackingOffset(float offset) {
  f_tracking_offset = offset;
  b_tracking_active = true;
  Serial.print("Manual tracking offset set: ");
  Serial.println(offset, 4);
}

void setManualStableValue(float value) {
  f_previous_stable_value = value;
  t_last_stable_change = millis();
  Serial.print("Manual stable value set: ");
  Serial.println(value, 4);
}


void loop() {
  processWsPendingCmds();
  processBleStatusResponse();
  processBleVoltageResponse();

  if (b_powerOff){
    shut_down_now_nobeep();
    return;
  }

  const unsigned long now = millis();
  if (bleHasLiveClient()
      && b_requireHeartBeat
      && now - t_firstConnect > HEARTBEAT_TIMEOUT
      && now - t_heartBeat > HEARTBEAT_TIMEOUT
      && (t_lastDisconnectAttempt == 0
          || now - t_lastDisconnectAttempt >= HEARTBEAT_DISCONNECT_RETRY_INTERVAL)) {
    disconnectBLE();
  }
#ifdef HEARTBEATICON
  if (millis() - t_heartBeat < 500)
    b_heartBeatIcon = true;
  else
    b_heartBeatIcon = false;
#endif
  if (b_ota || bleHasLiveClient()
#if HDS_FEATURE_WEBSOCKET
      || (b_wifiEnabled && websocket.count() > 0)
#endif
#if HDS_ENABLE_GRINDER
      || grinderRuntimeKeepsAwake()
#endif
  ) {
    power_off(-1);
  } else {
    power_off(15);
  }
  if (!b_ota && Serial.available()) {
    uint8_t data[32];
    size_t len = 0;
    while (Serial.available() && len < sizeof(data)) {
      data[len++] = Serial.read();
    }
    usbCallbacks.onStream(data, len);
  }
  if (!b_ota) {
    usbCallbacks.poll();
  }

  if (!b_ota
      && !buttonChecksSuppressedUntilRelease()
#if HDS_ENABLE_GRINDER
      && !handleGrinderMenuChord()
#endif
  ) {
    buttonCircle.check();
    buttonSquare.check();
  }
#ifdef BUZZER
  buzzer.check();
#endif

  if (b_ota && b_softSleep) {
    wakeScaleFromSoftSleep("OTA wake");
  }
  if (!b_softSleep) {
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    if (b_gyroEnabled) {
#ifdef GYROFACEUP
      if (gyro_z() > 5)
        power_off_gyro(-1);
#endif
#ifdef GYROFACEDOWN
      if (gyro_z() < -5)
        power_off_gyro(-1);
#endif
      power_off_gyro(10);
    }
#endif
#if defined(DEBUG) && defined(CHECKBATTERY)
    debugData();
#endif  //DEBUG
    if (millis() - t_batteryRefresh > i_batteryRefreshTareInterval){
      updateBattery(BATTERY_PIN);
    }
    checkBattery();
    if (b_ota) {
#if HDS_FEATURE_ELEGANT_OTA
      ElegantOTA.loop();
      processOtaDisplayUpdate();
#endif
      return;
    }
    if (b_menu) {
#if HDS_FEATURE_WIFI
      if (b_wifiEnabled) {
        wifiSupervise();
#if !HDS_FEATURE_WEBSERVER
        wifiConfigServerPoll();
#endif
      }
#endif
#if HDS_ENABLE_GRINDER
      grinderRuntimeTick(f_displayedValue);
#endif
      showMenu();
    } else if (GPIO_power_on_with == BATTERY_CHARGING) {
      if (b_chargingOLED) {
        if (digitalRead(BATTERY_CHARGING) == LOW && !b_calibration) {
          float perc = map(f_batteryVoltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);
          chargingOLED((int)perc, f_batteryVoltage);
          b_showChargingUI = true;
        } else {
          b_showChargingUI = false;
          bool b_usbDisconnected = false;
#ifdef USB_DET
          b_usbDisconnected = digitalRead(USB_DET) == HIGH;
#endif
          if (b_usbDisconnected) {
            Serial.println("USB unplugged. Entering scale mode.");
            GPIO_power_on_with = BUTTON_SQUARE;
            b_is_charging = false;
            scale.powerUp();
            if (refreshScaleDatasetAfterDiscontinuity("charging wake") &&
                tareScaleWhenAdcReady("charging wake tare")) {
              resetScaleOutputAfterAdcDiscontinuity();
            }
          } else {
            if (f_batteryVoltage > 4.1) {
              Serial.println("Charging complete.");
            } else {
              Serial.println("Charging stopped before full.");
            }
            b_powerOff = true;
            Serial.println("Going to sleep now by BatteryFull");
          }
        }
      }
    } else {
      if (b_calibration == true) {
        calibration(i_calibration);
      } else if (b_usbLinked == true) {
      } else {
#if HDS_FEATURE_WIFI
        if (b_wifiEnabled) {
          wifiSupervise();
#if !HDS_FEATURE_WEBSERVER
          wifiConfigServerPoll();
#endif
#if HDS_FEATURE_WEBSOCKET
          websocket.cleanupClients(4);
#endif
#if HDS_FEATURE_ELEGANT_OTA
          ElegantOTA.loop();
          processOtaDisplayUpdate();
#endif
        }
#endif

        g_timerRunning = stopWatch.isRunning();
        g_timerElapsed = (unsigned long)stopWatch.elapsed();

        if (!b_adc_recovery_active) {
          static unsigned long t_weightTick = 0;
          static unsigned long weightTickCount = 0;
          unsigned long nowMs = millis();
          if (nowMs - t_weightTick >= WEIGHT_BASE_INTERVAL_MS) {
            t_weightTick += WEIGHT_BASE_INTERVAL_MS;
            if (nowMs - t_weightTick >= WEIGHT_BASE_INTERVAL_MS) t_weightTick = nowMs;
            weightTickCount++;
            if (weightTickCount % max(1UL, weightTextNotifyInterval / WEIGHT_BASE_INTERVAL_MS) == 0)
              sendUsbTextWeight();
            if (b_ble_enabled &&
                weightTickCount % max(1UL, weightBleNotifyInterval / WEIGHT_BASE_INTERVAL_MS) == 0)
              sendBleWeight();
            if (b_usbweight_enabled &&
                weightTickCount % max(1UL, weightUsbNotifyInterval / WEIGHT_BASE_INTERVAL_MS) == 0)
              sendUsbWeight();
#if HDS_FEATURE_WEBSOCKET
            if (b_wifiEnabled &&
                weightTickCount % max(1UL, weightWebsocketNotifyInterval / WEIGHT_BASE_INTERVAL_MS) == 0)
              sendWebsocketWeightAll(f_displayedValue, nowMs);
#endif
          }
        }

        if (b_ble_enabled && bleHasLiveClient() && bleDebugMode != DEBUG_OFF) {
          if (bleDebugMode == DEBUG_SINGLE ||
              millis() - t_lastBleDebugNotify >= BLE_DEBUG_MIN_INTERVAL) {
            t_lastBleDebugNotify = millis();
            sendAdsDebugInfoBLE();
          }
        }
        if (b_bootTare) {
          processBootFreshTare();
        } else if (b_tareByButton) {
          if (millis() - t_tareByButton > i_tareDelay) {
            bool tareDone = tareScaleWhenAdcReady("button tare", true);
            b_tareByButton = false;
            Serial.println(tareDone ? "Tare by button" : "Tare by button failed");
          }
        } else if (hasRemoteTareRequest()) {
          uint8_t remoteTareRequests = consumeRemoteTareRequests();
          bool tareDone = tareScaleWhenAdcReady("remote tare", true);
          if (tareDone) {
            Serial.print("Tare by remote command");
            if (remoteTareRequests > 1) {
              Serial.print(" (");
              Serial.print(remoteTareRequests);
              Serial.print(" requests)");
            }
            Serial.println();
          } else {
            Serial.println("Tare by remote command failed");
          }
        }
        pureScale();
        updateOled();
#if HDS_ENABLE_GRINDER
        grinderRuntimeTick(f_displayedValue);
#endif
      }
    }
  }
}


void chargingOLED(int perc, float voltage) {
  if (millis() - t_oled_refresh >= 1000) {
    t_oled_refresh = millis();
    u8g2.firstPage();
    do {
      u8g2.drawXBM(121, 51, 7, 12, image_battery_charging);
    } while (u8g2.nextPage());
  }








}


void updateOled() {
  if (millis() - t_oled_refresh >= i_oled_print_interval) {
    t_oled_refresh = millis();

    u8g2.firstPage();
    do {
      if (b_screenFlipped)
        u8g2.setDisplayRotation(U8G2_R0);
      else
        u8g2.setDisplayRotation(U8G2_R2);
      u8g2.setFontMode(1);
      u8g2.setDrawColor(1);
      if (!b_debug) {
        if (b_adc_recovery_active) {
          drawAdcRecovery();
        } else {
          drawWeight(f_displayedValue);
          drawTime();
        }
      }
      u8g2.setDrawColor(2);

      drawBattery();
      drawButton();
      drawBle();
      drawHeartBeat();
#if HDS_ENABLE_GRINDER
      drawGrinder();
#endif
      drawTare();
      drawShutdownFail();
      drawAbout();
      drawDebug();

    } while (u8g2.nextPage());
  }
}

void drawShutdownFail() {
  if (b_shutdownFailBle && millis() - t_shutdownFailBle < 3000 && millis() - t_menuExitTime > 1000) {
    u8g2.setFont(FONT_S);
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 0, 128, 64);
    u8g2.setDrawColor(1);
    u8g2.drawStr(AC((char *)"Power off with app"), AM() - 25, (char *)"Power off with app");
    u8g2.drawStr(AC((char *)"(Or tap [] now"), AM(), (char *)"(Or tap [] now");
    u8g2.drawStr(AC((char *)"to force off)"), AM() + 25, (char *)"to force off)");
  }
  if (b_shutdownFailBle && millis() - t_shutdownFailBle >= 3000) {
    b_shutdownFailBle = false;
  }
}

int i_weightInt;
int i_weightFirstDecimal;
void separateFloat(float number) {
  int roundedTenths = (int)roundf(number * 10.0f);
  b_negativeWeight = roundedTenths < 0;
  i_weightInt = roundedTenths / 10;
  i_weightFirstDecimal = abs(roundedTenths % 10);
}

void drawWeight(float input) {
  if (input > OVER_WEIGHT){
    u8g2.setFont(FONT_GRAM);
    u8g2.drawStr(AC((char *)"Over"), AT(), (char *)"Over");
    u8g2.drawStr(AC((char *)"Weight"), AT() + 20, (char *)"Weight");
  }
  else {
    separateFloat(input);
    char integerStr[10] = "-0";
    char decimalStr[10] = "0";
    if (!b_negativeWeight || i_weightInt <= -1) {
      snprintf(integerStr, sizeof(integerStr), "%d", i_weightInt);
    }
    snprintf(decimalStr, sizeof(decimalStr), "%d", i_weightFirstDecimal);

    u8g2.setFont(FONT_TIMER);
    int y_timer = u8g2.getMaxCharHeight();
    u8g2.setFont(FONT_GRAM);
    int gramWidth = u8g2.getUTF8Width("g");
    u8g2.setFont(FONT_WEIGHT);
    int integerWidth = u8g2.getUTF8Width(trim(integerStr));
    int decimalWidth = u8g2.getUTF8Width(trim(decimalStr));
    int decimalPointWidth = u8g2.getUTF8Width(".");
    if (input <= -1000.0)
      gramWidth = 0;
    int x_integer = (128 - (integerWidth + decimalWidth + gramWidth + decimalPointWidth - 6 - 1)) / 2;
    int x_decimalPoint = x_integer + integerWidth - 4;
    int x_decimal = x_decimalPoint + decimalPointWidth - 4;
    int x_gram = x_decimal + decimalWidth - 1;
    int y = AT() - 15;
    if (b_timeOnTop)
      y = AT() + 9;
    if (strcmp(sec2sec(stopWatch.elapsed()), "0s") == 0) {
      y = AT();
    }
    u8g2.drawStr(x_decimalPoint, y, ".");
    u8g2.drawStr(x_integer, y, trim(integerStr));
    u8g2.drawStr(x_decimal, y, trim(decimalStr));
    if (input > -1000.0) {
      u8g2.setFont(FONT_GRAM);
      u8g2.drawStr(x_gram, y - 5, "g");
    }
  }
}

void drawTime() {
  if (strcmp(sec2sec(stopWatch.elapsed()), "0s") != 0) {
    u8g2.setFont(FONT_TIMER);
    int y = LCDHeight - 8;
    if (b_timeOnTop)
      y = u8g2.getMaxCharHeight() - 8;
    u8g2.drawStr(AC(sec2sec(stopWatch.elapsed())), y, sec2sec(stopWatch.elapsed()));
  }
}

void drawAdcRecovery() {
  u8g2.setFont(FONT_S);
  const char* line1 = getAdcRecoveryDisplayText();
  const char* line2 = "CHECK SCALE";
  u8g2.drawStr(AC(line1), AM() - 12, line1);
  u8g2.drawStr(AC(line2), AM() + 12, line2);
}

const char* getAdcRecoveryDisplayText() {
  return i_adc_recovery_count >= ADC_ERROR_RECOVERY_COUNT ? "ADC ERROR" : "ADC RECOVER";
}

void drawButton() {
  if (digitalRead(BUTTON_CIRCLE) == LOW) {
    if (b_screenFlipped)
      u8g2.drawXBM(113, 0, 15, 16, image_circle);
    else
      u8g2.drawXBM(0, 0, 15, 16, image_circle);
  }
  if (digitalRead(BUTTON_SQUARE) == LOW)
    if (b_screenFlipped)
      u8g2.drawXBM(0, 0, 14, 16, image_square);
    else
      u8g2.drawXBM(114, 0, 14, 16, image_square);
}

unsigned long t_ble_box = 0;
bool b_drawBle = false;
void drawBle() {
  if (b_ble_enabled) {
    if (bleHasLiveClient()) {
      u8g2.drawXBM(3, 51, 5, 13, image_ble_enabled);
    } else {
      if (millis() - t_ble_box > 1000) {
        b_drawBle = !b_drawBle;
        t_ble_box = millis();
      }
      if (b_drawBle)
        u8g2.drawXBM(3, 51, 5, 13, image_ble_enabled);
    }
  } else {
    u8g2.drawXBM(0, 51, 10, 13, image_ble_disabled);
  }
}

void drawHeartBeat(){
  if (b_heartBeatIcon)
    u8g2.drawXBM(30, 51, 13, 13, image_heart_13x13);
}

#if HDS_ENABLE_GRINDER
void drawGrinder() {
  if (!grinderSettings.enabled) {
    return;
  }
  char text[28];
  grinderShortStatus(text, sizeof(text));
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(46, 64, text);
}
#endif

void drawBattery() {
  if (millis() - t_batteryIcon >= 500) {
    t_batteryIcon = millis();
    b_showBatteryIcon = !b_showBatteryIcon;
  }
#if defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  if (digitalRead(USB_DET) == LOW) {
#else
  if (digitalRead(BATTERY_CHARGING) == LOW) {
#endif
    b_is_charging = true;
    u8g2.drawXBM(121, 51, 7, 12, image_battery_charging);
  } else {
    b_is_charging = false;
    int i_batteryPercent = map(f_batteryVoltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);
    if (i_batteryPercent <= 5) {
      if (b_showBatteryIcon)
        u8g2.drawXBM(121, 52, 7, 12, image_battery_0);
    } else if (i_batteryPercent > 5 && i_batteryPercent <= 25) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_1);
    } else if (i_batteryPercent > 25 && i_batteryPercent <= 50) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_2);
    } else if (i_batteryPercent > 50 && i_batteryPercent <= 75) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_3);
    } else if (i_batteryPercent > 75) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_4);
    }
  }

#if HDS_FEATURE_WIFI
  if (b_wifiEnabled) {
    bool connecting = WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED;
    if (!connecting || (millis() / 500) % 2) {
      u8g2.setFont(u8g2_font_open_iconic_www_1x_t);
      int glyph = WiFi.getMode() == WIFI_STA ? 0x004F : 0x0051;
      u8g2.drawGlyph(10, 64, glyph);
    }
  }
#endif
}

void drawAbout() {
  if (b_about) {
    u8g2.setFont(FONT_M);
    if (AC(FIRMWARE_VER) < 0 || AC(PCB_VER) < 0)
      u8g2.setFont(FONT_S);
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 0, LCDWidth, LCDHeight);
    u8g2.setDrawColor(1);
    u8g2.drawStr(AC(FIRMWARE_VER), AM() - 12, FIRMWARE_VER);
    u8g2.drawStr(AC(PCB_VER), AM() + 12, PCB_VER);
    if (digitalRead(BUTTON_SQUARE) == LOW)
      b_about = false;
  }
}

void drawDebug() {
  if (b_debug) {
    char bleText[20];
    if (b_ble_enabled) {
      if (bleHasLiveClient()) {
        snprintf(bleText, sizeof(bleText), "BLE connected");
      } else {
        snprintf(bleText, sizeof(bleText), "BLE enabled");
      }
    } else {
      snprintf(bleText, sizeof(bleText), "BLE disabled");
    }

    char chargingText[20];
    if (digitalRead(BATTERY_CHARGING) == LOW)
      snprintf(chargingText, sizeof(chargingText), "Charging");
    else
      snprintf(chargingText, sizeof(chargingText), "Not charging");

    char batteryText[10];
    int perc = map(f_batteryVoltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);
    snprintf(batteryText, sizeof(batteryText), "%d%%", (perc > 100) ? 100 : perc);

    char voltageText[10];
    if (perc > 100) {
      strcat(batteryText, "+");
    }
    snprintf(voltageText, sizeof(voltageText), "%.1fV", f_batteryVoltage);

    char gpioText[10];
    snprintf(gpioText, sizeof(gpioText), "GPIO:%d", i_wakeupPin);

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    char gyroText[10];
    snprintf(gyroText, sizeof(gyroText), "Gyro:%.1f", gyro_z());
#endif

    char weightText[10];
    snprintf(weightText, sizeof(weightText), "%.1fg", f_displayedValue);

    u8g2.setFont(u8g2_font_6x13_tr);
    int lineHeight = 12;
    u8g2.drawStr(-54, lineHeight, LINE1);
    u8g2.drawStr(0, lineHeight * 2, (char *)trim(gpioText));
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    u8g2.drawStr(0, lineHeight * 3, (char *)trim(gyroText));
#endif
    u8g2.drawStr(0, lineHeight * 4, (char *)trim(chargingText));
    u8g2.drawStr(14, lineHeight * 5, (char *)trim(bleText));

    u8g2.drawStr(55, lineHeight, (char *)trim(batteryText));
    u8g2.drawStr(55, lineHeight * 2, (char *)trim(voltageText));

    u8g2.drawStr(AR((char *)trim(weightText)), lineHeight, (char *)trim(weightText));
    u8g2.drawStr(AR(sec2sec(stopWatch.elapsed())), lineHeight * 2, sec2sec(stopWatch.elapsed()));

  }
}

void drawTare() {
  if (millis() - t_tareStatus < 500) {
    u8g2.drawBox(30, 62, 128 - 30 * 2, 2);
  }
}

void drawDriftCompensationInfo() {
  char factorText[20];
  u8g2.setFont(u8g2_font_6x13_tr);

  snprintf(factorText, sizeof(factorText), "TUI:%lums", TRACKING_UPDATE_INTERVAL);
  u8g2.drawStr(0, 13, (char *)trim(factorText));
  snprintf(factorText, sizeof(factorText), "TT:%.2f", TRACKING_THRESHOLD);
  u8g2.drawStr(AR((char *)trim(factorText)), 13, (char *)trim(factorText));

  snprintf(factorText, sizeof(factorText), "%.3f", f_maxDriftCompensation);
  u8g2.drawStr(0, 26, (char *)"MDC");
  u8g2.drawStr(0, 39, (char *)trim(factorText));

  snprintf(factorText, sizeof(factorText), "TDC:%.2f", f_driftCompensation * -1);
  u8g2.drawStr(AR((char *)trim(factorText)), 26, (char *)trim(factorText));

  snprintf(factorText, sizeof(factorText), "RAW:%.2f", f_current_raw_value);
  u8g2.drawStr(12, 64, (char *)trim(factorText));
  snprintf(factorText, sizeof(factorText), "%.2f", f_displayedValue);
  u8g2.drawStr(80, 64, (char *)trim(factorText));
}
