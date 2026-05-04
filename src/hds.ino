#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

#include "parameter.h"
#include "power.h"
#include "gyro.h"
#include "display.h"
#include "declare.h"
#include "wifi_setup.h"
#include "webserver.h"
#include "wifi_ota.h"


#include "menu.h"
#include "ble.h"
#include "usbcomm.h"
#include "finger_detection.h"
//#include "wificomm.h"

// ADS1232 Debug Callback - called every time a new conversion is ready
void adsDebugCallback(const ADS1232DebugInfo& info) {
  // This will be called frequently, so you may want to throttle output
  static unsigned long lastDebugPrint = 0;
  unsigned long now = millis();
  
  // Print debug info every 1000ms (adjust as needed)
  if (now - lastDebugPrint >= 1000) {
    Serial.println("=== ADS1232 Debug Info ===");
    Serial.print("Timestamp: "); Serial.println(info.timestamp);
    Serial.print("Raw Value: "); Serial.print(info.rawValue);
    Serial.print(" | Smoothed: "); Serial.println(info.smoothedValue);
    Serial.print("Tare Offset: "); Serial.println(info.tareOffset);
    Serial.print("Conv Time: "); Serial.print(info.conversionTime, 3);
    Serial.print("ms | SPS: "); Serial.println(info.sps, 2);
    Serial.print("Samples: "); Serial.print(info.samplesInUse);
    Serial.print(" | Read Index: "); Serial.println(info.readIndex);
    
    // Data statistics - useful for detecting noise/instability
    Serial.print("Dataset - Min: "); Serial.print(info.dataMin);
    Serial.print(" | Max: "); Serial.print(info.dataMax);
    Serial.print(" | Avg: "); Serial.println(info.dataAvg);
    Serial.print("Std Dev: "); Serial.print(info.dataStdDev, 2);
    Serial.println(" (lower = more stable)");
    
    // Error flags
    Serial.print("Flags - OutOfRange: "); Serial.print(info.dataOutOfRange);
    Serial.print(" | SignalTimeout: "); Serial.print(info.signalTimeout);
    Serial.print(" | Tare: "); Serial.print(info.tareInProgress);
    if (info.tareInProgress) {
      Serial.print(" ("); Serial.print(info.tareTimes); Serial.print("/"); Serial.print(DATA_SET); Serial.print(")");
    }
    Serial.println();
    Serial.println("==========================");
    
    lastDebugPrint = now;
  }
}

// Reads a boolean value from EEPROM with validation.
// If the stored value is not 0 or 1 (i.e., invalid or uninitialized data),
// it will be replaced with the provided default value.
bool readBoolEEPROMWithValidation(int addr, bool defaultVal) {
  uint8_t val;
  EEPROM.get(addr, val);  // Read raw byte from EEPROM
  if (val == 0 || val == 1) {
    // Valid boolean value found
    return val;
  }
  // Invalid value, overwrite with default
  EEPROM.put(addr, (uint8_t)defaultVal);
  EEPROM.commit();
  return defaultVal;
}


//buttons

void aceButtonHandleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  power_off(-1);  //reset power off timer
  if (b_u8g2Sleep) {
    u8g2.setPowerSave(0);  //wake up oled when button is pressed
    b_u8g2Sleep = false;
  }
  if (b_softSleep) {
    Serial.println("Exit Soft Sleep.");
    digitalWrite(PWR_CTRL, HIGH);
    //#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    digitalWrite(ACC_PWR_CTRL, HIGH);
    //#endif
    u8g2.setPowerSave(0);
    b_softSleep = false;
  }
  u8g2.setContrast(255);  //set oled brightness to max when button is pressed
  int pin = button->getPin();
  switch (eventType) {
    case AceButton::kEventPressed:
//these will be triggered once the button is touched.
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
    // case AceButton::kEventClicked:
    //   //these will only be triggerd once a click and released is performed.
    //   switch (pin) {
    //     case BUTTON_CIRCLE:
    //       buttonCircle_Clicked();
    //       break;
    //     case BUTTON_SQUARE:
    //       buttonSquare_Clicked();
    //       break;
    //   }
    //   break;
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
    //进入萃取模式1s后可以操作的时钟
    if (stopWatch.elapsed() == 0) {
      //初始态 时钟开始
      stopWatch.start();
      Serial.println("Timer Start");  //timer start
      //delay(500); ?? why delay cause stopWatch.start() not running, but sending set via uart and 030B03 can trigger that?
    }
    if (stopWatch.elapsed() > 0) {
      //停止态 时钟清零
      stopWatch.reset();
      Serial.println("Timer Reset");  //timer reset
    }
  } else {
    //在计时中 按一下则结束计时 停止冲煮 （固定参数回头再说）
    stopWatch.stop();
    Serial.println("Timer Stop");
  }
}

void buttonCircle_Released() {
  Serial.println("O button released");
  onButtonReleased(BUTTON_CIRCLE);
}

void buttonCircle_Pressed() {
  if (b_showChargingUI && i_buttonBootDelay == 0) {
    //change GPIO_power_on_with from BATTERY_CHARGING to enter scale loop
    GPIO_power_on_with = BUTTON_CIRCLE;
    b_showChargingUI = false;
    b_ble_enabled = true;
    ble_init();
    wifi_init();
  }

  if (b_menu) {
    navigateMenu(1);  // Navigate to next menu item
  }
  if (b_calibration) {
    i_cal_weight++;
    Serial.print("i_cal_weight = ");
    Serial.println(i_cal_weight);
    if (i_cal_weight > 5)
      i_cal_weight = 0;
    //This will increment i_cal_weight by 1 and wrap around to 0 when it reaches 4, effectively keeping it within the range of 0 to 3.
    // return;
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
    GPIO_power_on_with = BUTTON_SQUARE;
  }
  if (b_menu) {
    selectMenu();
  }
  if (b_calibration) {
    i_button_cal_status++;
    Serial.print("i_button_cal_status:");
    Serial.println(i_button_cal_status);
  }
  if (deviceConnected && millis() - t_shutdownFailBle < 3000 && !b_menu && millis() - t_menuExitTime > 1000) {
    //millis() - t_menuExitTime > 1000 is to avoid instant Off when exiting menu.
    Serial.println("Going to sleep now by SquarePress");
    stopWebServer();
    stopWifi();
    b_powerOff = true;
  }
  startPressSampling(BUTTON_SQUARE);
}

// Individually set sensitivity parameters for each button
void setButtonPressConfig(int button, float min_peak, float max_net, 
                         float min_recovery, unsigned long max_press_time,
                         unsigned long min_total_time) {
  // Can add code here to save configurations via EEPROM or other methods
  // Currently using predefined macros, can be changed to variables later
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

// void buttonCircle_Clicked() {
//   Serial.println("O button short pressed");
//   sendUsbButton(1, 1);
//   if (deviceConnected) {
//     sendBleButton(1, 1);
//   } else {
//     // b_weight_quick_zero = true;
//     // t_tareByButton = millis();
//     // b_tareByButton = true;
//   }
// }

// void buttonSquare_Clicked() {
//   Serial.println("[] button short pressed");
//   sendUsbButton(2, 1);
//   if (deviceConnected) {
//     sendBleButton(2, 1);
//   }
// }

void buttonCircle_DoubleClicked() {
  Serial.println("O button double clicked");
  if (!deviceConnected && !b_menu && !b_calibration) {
    Serial.println("Going to sleep now by CircleDoubleClick");
    sendBlePowerOff(1);
    stopWebServer();
    stopWifi();
    b_powerOff = true;
  } else {
    if (deviceConnected) {
      t_shutdownFailBle = millis();
      b_shutdownFailBle = true;
      Serial.println("BLE connected, not going to sleep.");
    }
    if (b_menu)
      Serial.println("Menu operating, not going to sleep.");
    if (!b_menu)
      sendBlePowerOff(0);
  }
}

void buttonSquare_DoubleClicked() {
  Serial.println("[] button double clicked");
  if (!deviceConnected && !b_menu && !b_calibration) {
    Serial.println("Going to sleep now by SquareDoubleClick");
    sendBlePowerOff(2);
    stopWebServer();
    stopWifi();
    b_powerOff = true;
  } else {
    if (deviceConnected) {
      t_shutdownFailBle = millis();
      b_shutdownFailBle = true;
      Serial.println("BLE connected, not going to sleep.");
    }
    if (b_menu)
      Serial.println("Menu operating, not going to sleep.");
    if (!b_menu)
      sendBlePowerOff(0);
  }
}

void buttonCircle_LongPressed() {
  if (!b_menu) {
    Serial.println("O button long pressed");
#ifdef BUZZER
    buzzer.beep(1, 200);
#endif
    if (GPIO_power_on_with == BATTERY_CHARGING) {
      //change GPIO_power_on_with from BATTERY_CHARGING to enter scale loop
      GPIO_power_on_with = BUTTON_CIRCLE;
      b_ble_enabled = true;
      ble_init();
      wifi_init();
    }     
    // sendUsbButton(1, 2);
    // if (deviceConnected) {
    //   Serial.println("Send O button long pressed BLE command");
    //   sendBleButton(1, 2);
    // }
  }
}

void buttonSquare_LongPressed() {
  if (!b_menu) {
    Serial.println("[] button long pressed");
#ifdef BUZZER
    buzzer.beep(1, 200);
#endif
    if (GPIO_power_on_with == BATTERY_CHARGING) {
      //change GPIO_power_on_with from BATTERY_CHARGING to enter scale loop
      GPIO_power_on_with = BUTTON_SQUARE;
    }
    // sendUsbButton(2, 2);
    // if (deviceConnected) {
    //   Serial.println("Send [] button long pressed BLE command");
    //   sendBleButton(2, 2);
    // }
    //b_debug = false;
  }
}


void button_init() {
  pinMode(BUTTON_CIRCLE, INPUT_PULLUP);
  pinMode(BUTTON_SQUARE, INPUT_PULLUP);
  buttonCircle.init(BUTTON_CIRCLE);
  buttonSquare.init(BUTTON_SQUARE);
  config1.setEventHandler(aceButtonHandleEvent);
  config1.setFeature(ButtonConfig::kFeatureClick);
  //config1.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
  config1.setFeature(ButtonConfig::kFeatureDoubleClick);
  // config1.setFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
  config1.setFeature(ButtonConfig::kFeatureLongPress);
  //config1.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  // config1.setDoubleClickDelay(DOUBLECLICK);
  // config1.setLongPressDelay(LONGCLICK);
  config1.setClickDelay(CLICK_DELAY);
  config1.setDoubleClickDelay(DOUBLECLICK_DELAY);
  config1.setLongPressDelay(LONGPRESS_DELAY);
}

void _wifi_init(void *args) {
  b_wifiEnabled = true;
  setupWifi();
  startWebServer();
  wifiOta();
  websocket.onEvent([](
                      AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("Client %u connected\n", client->id());
      client->setCloseClientOnQueueFull(false);
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("Client %u disconnected\n", client->id());
    } else if (type == WS_EVT_ERROR) {
      Serial.printf("WebSocket error on client %u\n", client->id());
    } else if (type == WS_EVT_PONG) {
      Serial.printf("Pong received from client %u\n", client->id());
    }
    if (type == WS_EVT_DATA) {

      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      String msg = "";

      for (size_t i = 0; i < info->len; i++) {
        msg += (char)data[i];
      }
      Serial.print("Websocket recv: ");
      Serial.println(msg);
      if (msg == "tare") {
        b_tareByBle = true;
      }
    }
  });
  vTaskDelete(NULL);
}
void wifi_init() {
  if (!b_wifiOnBoot) {
    return;
  }
  xTaskCreate(_wifi_init, "Wifi Init Task", configMINIMAL_STACK_SIZE + 2048, NULL, 0, NULL);
}

MyUsbCallbacks usbCallbacks;

void setup() {
  Serial.begin(115200);
  while (!Serial)  // Wait for the Serial port to initialize (typically used in Arduino to ensure the Serial monitor is ready)
    ;
  if (!EEPROM.begin(512)) {
    Serial.println("EEPROM init failed!");
    while (1) {
      delay(1000);
    }
  }

  if (readBoolEEPROMWithValidation(i_addr_quickBoot, false))
        i_buttonBootDelay = 0;

  Serial.println("EEPROM init success");
  
  // Initialize USB callback function pointers
  usbCallbacks.setStableOutputThreshold = setStableOutputThreshold;
  usbCallbacks.setTrackingThreshold = setTrackingThreshold;
  usbCallbacks.setTrackingUpdateInterval = setTrackingUpdateInterval;
  usbCallbacks.buttonSquare_Pressed = buttonSquare_Pressed;
  usbCallbacks.buttonCircle_Pressed = buttonCircle_Pressed;
  button_init();
  linkSubmenus();
  pinMode(BATTERY_CHARGING, INPUT_PULLUP);
#if defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  pinMode(USB_DET, INPUT_PULLUP);
  // either esp32 rev change or diff in SDK? We get 
  // warnings in logs about incorrect pinMode
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
    //power on by button_square/battery_charging
    b_ble_enabled = false;
  }
  if (GPIO_power_on_with == -1) {
    //power on by serial port
    b_ble_enabled = true;
  }
  while (true && GPIO_power_on_with > 0) {
    if (i_buttonBootDelay == 0){
      Serial.println("Quick boot. Powering on...");
        // Execute power on logic
      break;  // Exit loop to continue with other code
    }

    if (digitalRead(GPIO_power_on_with) == LOW) {  // Button is pressed
      if (!b_button_pressed) {
        t_power_on_button = millis();
        b_button_pressed = true;  // Mark button as pressed
      }

      if (millis() - t_power_on_button >= i_buttonBootDelay) {
        Serial.println("Button held for 0.5 second. Powering on...");
        // Execute power on logic
        break;  // Exit loop to continue with other code
      }
    } else {
      Serial.println("Button released before 0.5 second.");
      Serial.println("Going to sleep now.");
      stopWebServer();
      stopWifi();
      esp32_sleep();
      break;  // Exit loop to enter sleep mode
      b_button_pressed = false;  // Reset mark
    }
  }
  gpio_hold_dis((gpio_num_t)PWR_CTRL);  // Disable GPIO hold mode for the specified pin, allowing it to be controlled
  pinMode(PWR_CTRL, OUTPUT);            // Set the PWR_CTRL pin as an output pin
  digitalWrite(PWR_CTRL, HIGH);         // Set the PWR_CTRL pin to HIGH, turning on the connected device or circuit
//#if defined(ACC_MPU6050) || defined(ACC_BMA400)
#if defined(V7_3) || defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  gpio_hold_dis((gpio_num_t)ACC_PWR_CTRL);  // Disable GPIO hold mode for the specified pin, allowing it to be controlled
  pinMode(ACC_PWR_CTRL, OUTPUT);            // Set the PWR_CTRL pin as an output pin
  digitalWrite(ACC_PWR_CTRL, HIGH);         // Set the PWR_CTRL pin to HIGH, turning on the connected device or circuit
  Serial.println("ACC_PWR_CTRL = HIGH");
#endif
//#endif
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
  EEPROM.get(i_addr_beep, b_beep);

  if (GPIO_power_on_with != BATTERY_CHARGING) {
    buzzer.beep(1, BUZZER_DURATION);
  }
#endif
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(255);
  u8g2.setFont(FONT_M);
  power_off(15);
#ifdef WELCOME
  EEPROM.get(i_addr_welcome, str_welcome);
  str_welcome.trim();
#endif
  b_screenFlipped = readBoolEEPROMWithValidation(i_addr_screenFlipped, false);
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);

  //welcome
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_logisoso22_tf);
    u8g2.drawStr(AC("Half"), 26, "Half");
    u8g2.drawBox(4, LCDHeight / 2, LCDWidth - 4 * 2, 2);
    u8g2.drawStr(AC("Decent"), LCDHeight - 2, "Decent");
  } while (u8g2.nextPage());
  //adc init
  unsigned long stabilizingtime = 500;
  //taring duration. longer for better reading.
  bool _tare = true;  //电子秤初始化去皮，如果不想去皮则设为false
  //whether the scale will tare on start.
  scale.begin();
  scale.setSamplesInUse(1);  //设置灵敏度 (SAMPLES=4 allows runtime change via hex cmd)
  scale.start(stabilizingtime, _tare);
  scale.setCalFactor(f_calibration_value);  //设置偏移量
  //set the calibration value
  //scale.setSamplesInUse(sample[i_sample]);  //设置灵敏度
  
  // Setup ADS1232 debug callback
  scale.setDebugCallback(adsDebugCallback);
  // Debug is off by default, enable with "adsdebug on" command

  // if (GPIO_power_on_with != BATTERY_CHARGING) {
  //   delay(500);
  // }

  stopWatch.setResolution(StopWatch::SECONDS);
  stopWatch.start();
  stopWatch.reset();
  EEPROM.get(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
  EEPROM.get(INPUTCOFFEEESPRESSO_ADDRESS, INPUTCOFFEEESPRESSO);
  EEPROM.get(i_addr_batteryCalibrationFactor, f_batteryCalibrationFactor);
  EEPROM.get(i_addr_mode, b_mode);
  EEPROM.get(i_addr_driftCompensation, f_maxDriftCompensation);


  //EEPROM.get(i_addr_debug, b_debug);

  if (isnan(INPUTCOFFEEPOUROVER)) {
    INPUTCOFFEEPOUROVER = 16.0;
    EEPROM.put(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
    EEPROM.commit();
  }
  if (isnan(INPUTCOFFEEESPRESSO)) {
    INPUTCOFFEEESPRESSO = 18.0;
    EEPROM.put(INPUTCOFFEEESPRESSO_ADDRESS, INPUTCOFFEEESPRESSO);
    EEPROM.commit();
  }
  if (isnan(f_maxDriftCompensation)) {
    f_maxDriftCompensation = 0.05;
    EEPROM.put(i_addr_driftCompensation, f_maxDriftCompensation);
    EEPROM.commit();
  }
#ifdef V7_2
  if (isnan(f_batteryCalibrationFactor) || f_batteryCalibrationFactor < 1.4 || f_batteryCalibrationFactor > 1.8) {
    f_batteryCalibrationFactor = 1.66;
    //33k and 100k divider resistor
    EEPROM.put(i_addr_batteryCalibrationFactor, f_batteryCalibrationFactor);
    EEPROM.commit();
  }
#else
  if (isnan(f_batteryCalibrationFactor) || f_batteryCalibrationFactor < 0.9 || f_batteryCalibrationFactor > 1.3) {
    f_batteryCalibrationFactor = 1.06;
    //33k and 100k divider resistor
    EEPROM.put(i_addr_batteryCalibrationFactor, f_batteryCalibrationFactor);
    EEPROM.commit();
  }
#endif
#ifdef BUZZER
  if (b_beep != 0 && b_beep != 1) {
    b_beep = 1;
    EEPROM.put(i_addr_beep, b_beep);
    EEPROM.commit();
  }
#endif

  b_requireHeartBeat = readBoolEEPROMWithValidation(i_addr_requireHeartBeat, true);
  b_timeOnTop = readBoolEEPROMWithValidation(i_addr_timeOnTop, false);
  b_btnFuncWhileConnected = readBoolEEPROMWithValidation(i_addr_btnFuncWhileConnected, false);
  b_autoSleep = readBoolEEPROMWithValidation(i_addr_autoSleep, true);
  // if (b_debug != 0 && b_debug != 1) {
  //   b_debug = false;
  //   EEPROM.put(i_addr_debug, b_debug);
  //   EEPROM.commit();
  // }
  if (b_mode > 1) {
    b_mode = 0;
    EEPROM.put(i_addr_mode, b_mode);
    EEPROM.commit();
  }
  if (b_mode < 0) {
    b_mode = 0;
    EEPROM.put(i_addr_mode, b_mode);
    EEPROM.commit();
  }

  //loadcell calibration value check
  EEPROM.get(i_addr_calibration_value, f_calibration_value);
  if (isnan(f_calibration_value)) {
    f_calibration_value = 1000.0;
    EEPROM.put(i_addr_calibration_value, f_calibration_value);  //set to default value
    EEPROM.commit();
  }
  scale.setCalFactor(f_calibration_value);  //设定校准值

#ifdef DEBUG
  if (digitalRead(BUTTON_DEBUG) == LOW && digitalRead(BUTTON_CIRCLE) == HIGH)
    b_debug = true;
  if (digitalRead(BUTTON_DEBUG) == LOW && digitalRead(BUTTON_CIRCLE) == LOW)
    b_debug_battery = true;
#endif  //DEBUG

//重新校准
//recalibration
#ifdef CAL
  if (digitalRead(BUTTON_CIRCLE) == HIGH && digitalRead(BUTTON_SQUARE) == LOW) {
    b_calibration = true;  //让按钮进入校准状态3
    //go to calibration status 3
    i_button_cal_status = 1;
    calibration(0);
    //calibration value is not valid, go to calibration procedure.
  }
#endif
  b_wifiOnBoot = readBoolEEPROMWithValidation(i_addr_enableWifiOnBoot, false);
  if (b_wifiOnBoot && GPIO_power_on_with != BATTERY_CHARGING) {
    wifi_init();
  }
  // Enter Menu
  if (digitalRead(BUTTON_CIRCLE) == LOW && digitalRead(BUTTON_SQUARE) == LOW) {
    b_menu = true;
    refreshOLED((char *)"HDS Setup", FONT_EXTRACTION);
    delay(1000);
  }

#if DEBUG
  Serial.print("digitalRead(BUTTON_CIRCLE):");
  Serial.print(digitalRead(BUTTON_CIRCLE));
  Serial.print("\tdigitalRead(BUTTON_CIRCLE):");
  Serial.println(digitalRead(BUTTON_CIRCLE));
#endif

  // if (digitalRead(BUTTON_CIRCLE) == LOW) {
  //   SerialBT.begin("H.D.S. BT Serial");
  // }
  Serial.print("Welcome: ");
  if (str_welcome.length() == 127)
    Serial.print(WELCOME1);
  else
    Serial.print(str_welcome);
  Serial.print("\t");
  Serial.print(WELCOME2);
  Serial.print("\t");
  Serial.println(WELCOME3);
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


  //Serial.println("Button:\tSQARE\tCIRCLE\tPOWER");
  Serial.println("Button:\tSQARE\tCIRCLE");
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(BUTTON_SQUARE);
  Serial.print("\t");
  Serial.println(BUTTON_CIRCLE);
  // Serial.print("\t");
  // Serial.println(GPIO_NUM_BUTTON_POWER);
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
}

/**
 * Enhanced adaptive tracking system
 * Tracks both zero and stable weights to prevent oscillation
 */
void updateAdaptiveTracking(float current_weight) {
  unsigned long current_time = millis();
  
  if (!b_tracking_enabled) {
    return;
  }
  
  // Calculate weight difference from current tracking target
  float weight_diff = current_weight - f_tracking_target;
  
  // Check if weight is stable (within tracking threshold)
  if (fabs(weight_diff) <= TRACKING_THRESHOLD) {
    i_stable_count++;
    
    // Update tracking target to slowly follow stable weights
    if (i_stable_count >= 3) { // Start adjusting target after 3 stable readings
      float adjustment = weight_diff * 0.1; // Slow adaptation
      
      // Limit maximum adjustment to prevent large jumps
      if (fabs(adjustment) > MAX_TRACKING_ADJUSTMENT) {
        adjustment = (adjustment > 0) ? MAX_TRACKING_ADJUSTMENT : -MAX_TRACKING_ADJUSTMENT;
      }
      
      f_tracking_target += adjustment;
   }
    
  } else {
    // Weight changed significantly - likely a real weight change
    i_stable_count = 0;
    b_tracking_active = false;
    
    // If weight change is large and persistent, update tracking target
    if (fabs(weight_diff) > TRACKING_THRESHOLD * 2) {
      // Consider this as a new stable weight after verification
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
  
  // Perform tracking adjustment when conditions are met
  if (i_stable_count >= i_STABLE_COUNT_THRESHOLD) {
    if (current_time - t_last_tracking_update >= TRACKING_UPDATE_INTERVAL) {
      performTrackingAdjustment(current_weight);
    }
  }
}

/**
 * Perform the actual tracking adjustment
 */
void performTrackingAdjustment(float current_weight) {
  float old_offset = f_tracking_offset;
  
  // Calculate new offset based on current weight and target
  float calculated_offset = current_weight - f_tracking_target;
  
  // Apply slow adaptation to prevent sudden changes
  f_tracking_offset = f_tracking_offset * 0.8 + calculated_offset * 0.2;
  
  // Activate tracking if not already active
  if (!b_tracking_active) {
    b_tracking_active = true;
  }
  
  // Debug output
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
  
  // Reset counters
  i_stable_count = i_STABLE_COUNT_THRESHOLD - 2; // Keep near threshold for continuous tracking
  t_last_tracking_update = millis();
}

/**
 * Verify if a weight is stable enough to be considered a new target
 */
bool verifyWeightStability(float current_weight) {
  static float last_verified_weight = 0.0;
  static int verification_count = 0;
  
  if (fabs(current_weight - last_verified_weight) <= TRACKING_THRESHOLD) {
    verification_count++;
  } else {
    verification_count = 0;
  }
  
  last_verified_weight = current_weight;
  
  // Require 3 consecutive stable readings to verify new weight
  return (verification_count >= 3);
}

/**
 * Apply tracking compensation to raw weight
 */
float applyTrackingCompensation(float raw_weight) {
  if (b_tracking_active && b_tracking_enabled) {
    return raw_weight - f_tracking_offset;
  }
  return raw_weight;
}

/**
 * Apply stable output filtering
 * Returns the same value if change is below threshold
 */
float applyStableOutput(float current_value) {
  if (!b_stable_output_enabled) {
    return current_value; // Bypass stable filtering if disabled
  }
  
  float change = fabs(current_value - f_previous_stable_value);
  
  // If change is significant, update the stable value
  if (change >= STABLE_OUTPUT_THRESHOLD) {
    f_previous_stable_value = current_value;
    t_last_stable_change = millis();
    
    // Debug output for significant changes
    if (b_weight_in_serial) {
      Serial.print("Output updated: ");
      Serial.print(current_value, 4);
      Serial.print("g (Change: ");
      Serial.print(change, 4);
      Serial.println("g)");
    }
  }
  
  // Always return the stable value (may be same as previous)
  return f_previous_stable_value;
}

void pureScale() {
  static bool b_newDataReady = 0;
  static float f_last_displayed = 0.0;  // Last displayed value
  static unsigned long t_lastScaleData = 0;
  static unsigned long t_lastScaleRecovery = 0;

  if (t_lastScaleData == 0) {
    t_lastScaleData = millis();
  }

  if (scale.update()) {
    b_newDataReady = true;
    t_lastScaleData = millis();
  } else if (scale.getSignalTimeoutFlag() &&
             millis() - t_lastScaleData > 1500 &&
             millis() - t_lastScaleRecovery > 5000) {
    Serial.println("Scale ADC timeout. Power cycling ADC.");
    scale.powerDown();
    delay(5);
    scale.powerUp();
    scale.tareNoDelay();
    resetTracking();
    resetStableOutput();
    f_driftCompensation = 0.0;
    f_displayedValue = 0.0;
    dtostrf(f_displayedValue, 7, i_decimal_precision, c_weight);
    t_lastScaleRecovery = millis();
    t_lastScaleData = millis();
  }
  
  if (b_newDataReady) {
    float raw_weight = scale.getData();
    updatePressSampling();
    f_current_raw_value = raw_weight; // Store for status display
    
    // Continuous temperature drift detection and compensation
    // 1. Calculate difference between current raw and displayed value
    float current_diff = raw_weight - f_displayedValue - f_driftCompensation;
    
    // 2. If difference is small (0.01g-0.1g), accumulate to continuous compensation
    if (fabs(current_diff) > 0.01 && fabs(current_diff) < f_maxDriftCompensation) {
      static int f_similar_diff_count = 0;
      static float f_last_diff = current_diff;
      
      // Check if continuous same direction change
      if ((f_last_diff * current_diff) > 0) {  // Same direction
        f_similar_diff_count++;
        
        // If continuous micro changes, increase continuous compensation
        if (f_similar_diff_count >= 3) {
          // Increase continuous compensation (slowly)
          f_driftCompensation += current_diff * 0.3;  // Compensate 30% each time
          
          // Limit compensation range
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
          
          f_similar_diff_count = 2;  // Keep partial count for continued detection
        }
      } else {
        // Direction changed, reset
        f_similar_diff_count = 1;
      }
      
      f_last_diff = current_diff;
    } else {
      // Difference too large or too small, reset detection
      static int f_similar_diff_count = 0;
      f_similar_diff_count = 0;
    }
    
    // 3. Apply continuous temperature compensation
    float temperature_compensated = raw_weight - f_driftCompensation;
    
    // 4. Original processing pipeline
    float tracking_compensated = applyTrackingCompensation(temperature_compensated);
    float stable_output = applyStableOutput(tracking_compensated);
    
    if (stable_output >= -0.14 && stable_output <= 0.14) {
      f_displayedValue = 0.0;
    } else {
      // scale value is outside tolerance range, update displayed value
      f_displayedValue = stable_output;
    }
    // Update adaptive tracking (use temperature compensated value)
    updateAdaptiveTracking(tracking_compensated);
    
    // Display and debugging
    dtostrf(f_displayedValue, 7, i_decimal_precision, c_weight);
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
  
  // Reset temperature compensation on TARE
  if (scale.getTareStatus()) {
    t_tareStatus = millis();
    b_weight_quick_zero = false;
    resetTracking();
    resetStableOutput();
    f_driftCompensation = 0.0;
    f_displayedValue = 0.0;
    if (b_weight_in_serial) {
      Serial.println("TARE: Temperature drift compensation reset");
    }
  }
  
  // Quick zero handling
  if (b_weight_quick_zero || b_bootTare) {
    f_displayedValue = 0.0;
    f_driftCompensation = 0.0;
  }
}

/**
 * Get current temperature compensation value
 */
float getTemperatureDriftCompensation() {
  return f_driftCompensation;
}

/**
 * Manually adjust temperature compensation
 */
void adjustTemperatureDriftCompensation(float amount) {
  f_driftCompensation += amount;
  Serial.print("Manual temp-comp adjust: ");
  Serial.print(amount, 4);
  Serial.print("g, total: ");
  Serial.println(f_driftCompensation, 4);
}

/**
 * Reset tracking system (for tare/zero operations)
 */
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

/**
 * Reset stable output system
 */
void resetStableOutput() {
  f_previous_stable_value = 0.0;
  t_last_stable_change = millis();
  if (b_weight_in_serial) {
    Serial.println("Stable output reset");
  }
}

/**
 * Enable/disable stable output
 */
void setStableOutputEnabled(bool enabled) {
  b_stable_output_enabled = enabled;
  if (!enabled) {
    resetStableOutput();
  }
  Serial.print("Stable output ");
  Serial.println(enabled ? "enabled" : "disabled");
}

/**
 * Set stable output threshold
 */
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


/**
 * Enable/disable tracking system
 */
void setTrackingEnabled(bool enabled) {
  b_tracking_enabled = enabled;
  if (!enabled) {
    resetTracking();
  }
  Serial.print("Tracking system ");
  Serial.println(enabled ? "enabled" : "disabled");
}

/**
 * Enhanced status display with all system info
 */
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
  
  // Tracking status
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

/**
 * Get current tracking offset
 */
float getTrackingOffset() {
  return f_tracking_offset;
}

/**
 * Get current stable output value
 */
float getStableOutputValue() {
  return f_previous_stable_value;
}

// Optional: Manual control functions
/**
 * Manual override - set specific tracking offset
 */
void setManualTrackingOffset(float offset) {
  f_tracking_offset = offset;
  b_tracking_active = true;
  Serial.print("Manual tracking offset set: ");
  Serial.println(offset, 4);
}

/**
 * Manual override - set specific stable value
 */
void setManualStableValue(float value) {
  f_previous_stable_value = value;
  t_last_stable_change = millis();
  Serial.print("Manual stable value set: ");
  Serial.println(value, 4);
}


void loop() {
  if (b_powerOff){
    shut_down_now_nobeep();
    return;
  }

  if (bleState == CONNECTED && b_requireHeartBeat && millis() - t_firstConnect > HEARTBEAT_TIMEOUT) {
    if (millis() - t_heartBeat > HEARTBEAT_TIMEOUT) {
      disconnectBLE();
      t_heartBeat = millis() + 10000; //only disconnect after 10 seconds, avoid frequent disconnecting.
    } 
  }
#ifdef HEARTBEATICON
  if (millis() - t_heartBeat < 500)
    b_heartBeatIcon = true;
  else
    b_heartBeatIcon = false;
#endif
  if (deviceConnected) {
    power_off(-1);  //reset power off timer
  } else {
    //if (!b_tempDisablePowerOff)
    power_off(15);  //power off after 15 minutes
  }
  //serialCommand();
  if (Serial.available()) {
    uint8_t data[32];  // 假设最大接收 32 字节数据
    size_t len = 0;
    while (Serial.available() && len < sizeof(data)) {
      data[len++] = Serial.read();
    }
    usbCallbacks.onWrite(data, len);  // 调用 onWrite 函数处理串口数据
  }

  buttonCircle.check();
  buttonSquare.check();
#ifdef BUZZER
  buzzer.check();
#endif

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
      t_batteryRefresh = millis();
    }
    checkBattery();
    if (b_menu) {
      showMenu();
    } else if (GPIO_power_on_with == BATTERY_CHARGING) {
      if (b_chargingOLED) {
        if (digitalRead(BATTERY_CHARGING) == LOW && !b_calibration) {
          float perc = map(f_batteryVoltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);  //map funtion doesn't take float as input.
          chargingOLED((int)perc, f_batteryVoltage);
          b_showChargingUI = true;                                                                                  //show charging ui
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
            scale.tareNoDelay();
          } else {
            if (f_batteryVoltage > 4.1) {
              //charging complete
              Serial.println("Charging complete.");
            } else {
              //charging not complete, but the serial maynot be ouput cause usb unplugged.
              Serial.println("Charging stopped before full.");
            }
            stopWebServer();
            stopWifi();
            b_powerOff = true;  //deepsleep
            Serial.println("Going to sleep now by BatteryFull");
          }
        }
      }
    } else {
      if (b_ota) {
        ElegantOTA.loop();
        return;
      }
      if (b_calibration == true) {
        calibration(i_calibration);
      } else if (b_usbLinked == true) {
        //showing charging animation when powered off
        //charging();
      } else {
        sendUsbTextWeight();
        if (b_ble_enabled)
          sendBleWeight();
        if (b_usbweight_enabled)
          sendUsbWeight();
        if (b_wifiEnabled) {
          websocket.cleanupClients(1);
          ElegantOTA.loop();
          static long lastUpdate = 0;
          unsigned long current = millis();
          if (current - lastUpdate > 500) {
            if (websocket.availableForWriteAll() > 0) {
              websocket.printfAll("{ \"grams\": %.2f, \"ms\": %lu }", f_displayedValue, current);
            } else {
              Serial.println("Websocket write unavailable");
            }
            lastUpdate = current;
          }
        }
        if (b_bootTare) {
          //tare after boot
          if (millis() - t_bootTare > i_bootTareDelay) {
            scale.tareNoDelay();
            b_bootTare = false;
          }
        } else if (b_tareByButton) {
          // Tare by button, ensure 500ms delay to avoid touch interference
          if (millis() - t_tareByButton > i_tareDelay) {
            scale.tareNoDelay();
            b_tareByButton = false;  // reset status
            Serial.println("Tare by button");
          }
        } else if (b_tareByBle) {
          // Tare by BLE, performed instantly without delay
          scale.tareNoDelay();
          b_tareByBle = false;  // reset status
          Serial.println("Tare by BLE");
        }
        pureScale();
        updateOled();
      }
    }
  }
}


void chargingOLED(int perc, float voltage) {
  if (millis() > t_oled_refresh + 1000) {
    // Refresh the OLED at the specified interval
    t_oled_refresh = millis();
    u8g2.firstPage();
    do {
      u8g2.drawXBM(121, 51, 7, 12, image_battery_charging);  // charging icon
    } while (u8g2.nextPage());
  }
  // if (millis() > t_oled_refresh + 1000) {
  //   // Refresh the OLED at the specified interval
  //   t_oled_refresh = millis();
  //   u8g2.firstPage();
  //   do {
  //     if (b_screenFlipped)
  //       u8g2.setDisplayRotation(U8G2_R0);
  //     else
  //       u8g2.setDisplayRotation(U8G2_R2);

  //     // Get the display width and height
  //     int16_t displayWidth = u8g2.getDisplayWidth();
  //     int16_t displayHeight = u8g2.getDisplayHeight();

  //     // Set the size of the battery outline and inner rectangle
  //     int16_t width = 100, height = 30;
  //     int16_t innerWidth = width - 2;    // Width of the inner rectangle
  //     int16_t innerHeight = height - 2;  // Height of the inner rectangle

  //     // Calculate the centered position for the battery outline and inner rectangle
  //     int16_t x = (displayWidth - width) / 2;
  //     int16_t y = (displayHeight - height) / 2 - 6;

  //     u8g2.setFontMode(1);
  //     u8g2.setDrawColor(1);
  //     // Draw the battery outline (rounded rectangle)
  //     u8g2.drawRFrame(x, y, width, height, 5);  // Outline with a corner radius of 5

  //     // Calculate the height for the inner rectangle based on percentage
  //     int16_t innerY = y + 1 + innerHeight * (1 - (perc / 100.0));  // Y position of the inner rectangle
  //     //u8g2.drawBox(x + 1, y + 1, innerWidth * (perc / 100.0), innerHeight);  // Inner rectangle, filled to show charge level
  //     u8g2.drawVLine(x + width + 2, y + 7, height - 7 * 2);
  //     for (int i = 0; i < innerWidth - 1; i = i + 2) {
  //       if (i < (innerWidth * (perc / 100.0))) {
  //         u8g2.drawVLine(x + 2 + i, y + 2, innerHeight - 2);
  //       }
  //     }

  //     u8g2.setDrawColor(2);
  //     // Display the battery percentage
  //     char batteryText[10];
  //     snprintf(batteryText, sizeof(batteryText), "%d%%", (perc > 100) ? 100 : perc);
  //     if (perc > 100) {
  //       strcat(batteryText, "+");
  //     }
  //     u8g2.setFont(u8g2_font_ncenB12_tr);                                                                 // Set font
  //     u8g2.drawStr(x + (width / 4) - (u8g2.getStrWidth(batteryText) / 2), y + height + 15, batteryText);  // Center the percentage text

  //     // Display the voltage
  //     char voltageText[10];
  //     snprintf(voltageText, sizeof(voltageText), "%.1fV", voltage);
  //     u8g2.drawStr(x + (width / 4) + (width / 2) - (u8g2.getStrWidth(voltageText) / 2), y + height + 15, voltageText);  // Center the voltage text

  //   } while (u8g2.nextPage());
  // }
}


void updateOled() {
  if (millis() > t_oled_refresh + i_oled_print_interval) {
    //达到设定的oled刷新频率后进行刷新
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
        drawWeight(f_displayedValue);
        drawTime();
      }
      u8g2.setDrawColor(2);

      drawBattery();
      drawButton();
      drawBle();
      drawHeartBeat();
      drawTare();
      drawShutdownFail();
      drawAbout();
      drawDebug();
      //drawDriftCompensationInfo();

    } while (u8g2.nextPage());
  }
}

void drawShutdownFail() {
  if (b_shutdownFailBle && millis() - t_shutdownFailBle < 3000 && millis() - t_menuExitTime > 1000) {
    //millis() - t_menuExitTime > 1000 is to avoid showing ble connected press again to power off message
    u8g2.setFont(FONT_S);
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 0, 128, 64);
    u8g2.setDrawColor(1);
    u8g2.drawStr(AC((char *)"Power off with app"), AM() - 25, (char *)"Power off with app");  // Assuming vertical position at 28, adjust as needed
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
  // Extract the integer part
  i_weightInt = (int)number;
  if (number >= 0) {
    b_negativeWeight = false;
  } else {
    b_negativeWeight = true;
  }

  // Calculate the decimal part by subtracting the integer part from the number
  // and then multiplying by 10 to shift the first decimal digit into the integer place
  float decimalPart = number - (float)(i_weightInt);
  i_weightFirstDecimal = abs((int)(decimalPart * 10));
}

void drawWeight(float input) {
  /*new way
  int y;
  if (String(sec2sec(stopWatch.elapsed())) == "0s")
    y = AM();
  else
    y = AM() - 8;
  char c_temp[10];
  snprintf(c_temp, 7, "%.1f", input);
  u8g2.setFont(FONT_L);
  u8g2.drawStr(AC(trim((char *)c_temp)), y, trim((char *)c_temp));
  u8g2.setFont(FONT_M);
  u8g2.drawStr(AC(trim((char *)c_temp)) + u8g2.getUTF8Width((trim((char *)c_temp))) + 8, y - 4, "g");
  */
  /* old way
  separateFloat(input);
  char integerStr[10] = "-0";  // to save the - sign if the input is between -1 to 0
  char decimalStr[10] = "0";
  if (input >= 0 || input <= -1) {
    dtostrf(i_weightInt, 7, 0, integerStr);  // Integer part, no decimal
  }
  dtostrf(i_weightFirstDecimal, 7, 0, decimalStr);
  u8g2.setFont(FONT_GRAM);
  int gramWidth = u8g2.getUTF8Width("g");  // Width of the "g" character
  u8g2.setFont(FONT_WEIGHT);
  int integerWidth = u8g2.getUTF8Width(trim(integerStr));
  int decimalWidth = u8g2.getUTF8Width(trim(decimalStr));
  int decimalPointWidth = u8g2.getUTF8Width(".");
  int x_integer = (128 - (integerWidth + decimalWidth + gramWidth + decimalPointWidth - 6 - 1)) / 2;
  int x_decimalPoint = x_integer + integerWidth - 4;
  int x_decimal = x_decimalPoint + decimalPointWidth - 4;
  int x_gram = x_decimal + decimalWidth - 1;
  int y = AT() - 4;
  u8g2.drawStr(x_decimalPoint, y, ".");
  u8g2.drawStr(x_integer, y, trim(integerStr));  // Assuming vertical position at 28, adjust as needed
  u8g2.drawStr(x_decimal, y, trim(decimalStr));
  u8g2.setFont(FONT_GRAM);
  u8g2.drawStr(x_gram, y - 5, "g");
  */
  if (input > OVER_WEIGHT){
    u8g2.setFont(FONT_GRAM);
    u8g2.drawStr(AC((char *)"Over"), AT(), (char *)"Over");
    u8g2.drawStr(AC((char *)"Weight"), AT() + 20, (char *)"Weight");
  }
  else {
    separateFloat(input);
    char integerStr[10] = "-0";  // to save the - sign if the input is between -1 to 0
    char decimalStr[10] = "0";
    if (input >= 0 || input <= -1) {
      dtostrf(i_weightInt, 7, 0, integerStr);  // Integer part, no decimal
    }
    dtostrf(i_weightFirstDecimal, 7, 0, decimalStr);

    u8g2.setFont(FONT_TIMER);
    int y_timer = u8g2.getMaxCharHeight();
    u8g2.setFont(FONT_GRAM);
    int gramWidth = u8g2.getUTF8Width("g");  // Width of the "g" character
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
    int y = AT() - 15;  // Weight y when timer is shown.
    if (b_timeOnTop)
      y = AT() + 9;
    if (String(sec2sec(stopWatch.elapsed())) == "0s") {
      y = AT();  //Weight y when timer is hidden.
    }
    u8g2.drawStr(x_decimalPoint, y, ".");
    u8g2.drawStr(x_integer, y, trim(integerStr));  // Assuming vertical position at 28, adjust as needed
    u8g2.drawStr(x_decimal, y, trim(decimalStr));
    if (input > -1000.0) {
      u8g2.setFont(FONT_GRAM);
      u8g2.drawStr(x_gram, y - 5, "g");
    }
  }
}

void drawTime() {
  if (String(sec2sec(stopWatch.elapsed())) != "0s") {
    u8g2.setFont(FONT_TIMER);
    int y = LCDHeight - 8;
    if (b_timeOnTop)
      y = u8g2.getMaxCharHeight() - 8;
    u8g2.drawStr(AC(sec2sec(stopWatch.elapsed())), y, sec2sec(stopWatch.elapsed()));
  }
}

//button pressed box, left and right, added xor
void drawButton() {
  if (digitalRead(BUTTON_CIRCLE) == LOW) {
    // u8g2.drawBox(0, 0, 10, 64);
    if (b_screenFlipped)
      u8g2.drawXBM(113, 0, 15, 16, image_circle);
    else
      u8g2.drawXBM(0, 0, 15, 16, image_circle);
  }
  if (digitalRead(BUTTON_SQUARE) == LOW)
    //u8g2.drawBox(118, 0, 118, 64);
    if (b_screenFlipped)
      u8g2.drawXBM(0, 0, 14, 16, image_square);
    else
      u8g2.drawXBM(114, 0, 14, 16, image_square);
}

//some quick variable
unsigned long t_ble_box = 0;
bool b_drawBle = false;
//fake draw ble indicator box(bottom part)
void drawBle() {
  if (b_ble_enabled) {
    if (deviceConnected) {
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

void drawBattery() {
  if (millis() - t_batteryIcon >= 500) {  // Toggle every 500 ms
    t_batteryIcon = millis();
    b_showBatteryIcon = !b_showBatteryIcon;
  }
#if defined(V7_4) || defined(V7_5) || defined(V8_0) || defined(V8_1)
  //if (getUsbVoltage(USB_DET) > 4.0) {
  if (digitalRead(USB_DET) == LOW) {
#else
  if (digitalRead(BATTERY_CHARGING) == LOW) {
#endif
    b_is_charging = true;
    u8g2.drawXBM(121, 51, 7, 12, image_battery_charging);  // charging icon
    // Serial.println("Battery is charging");
  } else {
    b_is_charging = false;
    int i_batteryPercent = map(f_batteryVoltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);
    if (i_batteryPercent <= 5) {
      if (b_showBatteryIcon)
        u8g2.drawXBM(121, 52, 7, 12, image_battery_0);  // 0% or very low battery
    } else if (i_batteryPercent > 5 && i_batteryPercent <= 25) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_1);  // 5-25% battery
    } else if (i_batteryPercent > 25 && i_batteryPercent <= 50) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_2);  // 25-50% battery
    } else if (i_batteryPercent > 50 && i_batteryPercent <= 75) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_3);  // 50-75% battery
    } else if (i_batteryPercent > 75) {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_4);  // 75-100% battery
    }
  }

  // TODO: move to separate func?
  if (b_wifiEnabled) {
    u8g2.setFont(u8g2_font_open_iconic_www_1x_t);
    int glyph = WiFi.getMode() == WIFI_STA ? 0x004F : 0x0051;
    u8g2.drawGlyph(10, 64, glyph);
  }
}

void drawAbout() {
  if (b_about) {
    u8g2.setFont(FONT_M);
    if (AC(FIRMWARE_VER) < 0 || AC(PCB_VER) < 0)  //if info is too long then change to a smaller font
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
      if (deviceConnected) {
        snprintf(bleText, sizeof(bleText), "BLE connected");
      } else {
        snprintf(bleText, sizeof(bleText), "BLE enabled");
      }
    } else {
      snprintf(bleText, sizeof(bleText), "BLE disabled");
    }

    char chargingText[20];
    if (digitalRead(BATTERY_CHARGING) == LOW)  //low for charging
      snprintf(chargingText, sizeof(chargingText), "Charging");
    else
      snprintf(chargingText, sizeof(chargingText), "Not charging");

    char batteryText[10];
    int perc = map(f_batteryVoltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);  //map funtion doesn't take float as input.
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

    //display
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

    // char c_bat[10] = "";
    // dtostrf(f_batteryVoltage, 7, 2, c_bat);
    // u8g2.setFont(FONT_S);
    // u8g2.drawUTF8(14, 60, (char *)trim(c_bat));
    // #if defined(V7_4)
    //     char c_usb[10] = "";
    //     dtostrf(getUsbVoltage(USB_DET), 7, 2, c_usb);
    //     u8g2.setFont(FONT_S);
    //     u8g2.drawUTF8(40, 60, (char *)trim(c_usb));
    // #endif
  }
}

void drawTare() {
  if (millis() - t_tareStatus < 500) {
    // u8g2.setFont(FONT_S);
    // u8g2.drawStr(AC((char *)"TARE"), 60, (char *)"TARE");
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



