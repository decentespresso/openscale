#include <Arduino.h>
#include <EEPROM.h>
#include "parameter.h"
#include "power.h"
#include "gyro.h"
#include "display.h"
#include "declare.h"
#include "wifi_ota.h"
#include "config.h"
#include "menu.h"
#include "ble.h"
#include "usbcomm.h"
//#include "wificomm.h"

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
    digitalWrite(ACC_PWR_CTRL, HIGH);
    u8g2.setPowerSave(0);
    b_softSleep = false;
  }
  u8g2.setContrast(255);  //set oled brightness to max when button is pressed
  int pin = button->getPin();
  switch (eventType) {
    case AceButton::kEventPressed:
      //these will be triggered once the button is touched.
      if (GPIO_power_on_with != BATTERY_CHARGING)
        buzzer.beep(1, BUZZER_DURATION);
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
  //trigger tare after released to avoid to judge where the tare should be placed.
  if (!deviceConnected) {
    b_weight_quick_zero = true;
    t_tareByButton = millis();
    b_tareByButton = true;
  }
  Serial.println("O button released");
  sendUsbButton(1, 1);
  if (deviceConnected) {
    sendBleButton(1, 1);
  }
}

void buttonCircle_Pressed() {
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
  
}

void buttonSquare_Pressed() {
  if (b_menu) {
    selectMenu();  // Select current menu item
  }
  if (b_calibration) {
    i_button_cal_status++;
    Serial.print("i_button_cal_status:");
    Serial.println(i_button_cal_status);
    //return;
  }
  if (deviceConnected && millis() - t_shutdownFailBle < 3000)
    shut_down_now_nobeep();
  if (!b_menu && !b_calibration && !deviceConnected) {
    scaleTimer();
  }

  Serial.println("[] button pressed");
  sendUsbButton(2, 1);
  if (deviceConnected) {
    sendBleButton(2, 1);
  }
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
    Serial.println("Going to sleep now.");
    sendBlePowerOff(1);
    shut_down_now_nobeep();
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
    Serial.println("Going to sleep now.");
    sendBlePowerOff(2);
    shut_down_now_nobeep();
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
    buzzer.beep(1, 200);
    if (GPIO_power_on_with == BATTERY_CHARGING) {
      //change GPIO_power_on_with from BATTERY_CHARGING to enter scale loop
      GPIO_power_on_with = BUTTON_CIRCLE;
      b_ble_enabled = true;
      ble_init();
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
    buzzer.beep(1, 200);
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
  //config1.setFeature(ButtonConfig::kFeatureClick);
  //config1.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
  config1.setFeature(ButtonConfig::kFeatureDoubleClick);
  config1.setFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
  config1.setFeature(ButtonConfig::kFeatureLongPress);
  //config1.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  config1.setDoubleClickDelay(DOUBLECLICK);
  config1.setLongPressDelay(LONGCLICK);
}


void setup() {
  Serial.begin(115200);
  while (!Serial)  // Wait for the Serial port to initialize (typically used in Arduino to ensure the Serial monitor is ready)
    ;
  button_init();
  linkSubmenus();

  pinMode(BATTERY_CHARGING, INPUT_PULLUP);
#if defined(V7_4) || defined(V7_5) || defined(V8_0)
  pinMode(USB_DET, INPUT_PULLUP);
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
    if (digitalRead(GPIO_power_on_with) == LOW) {  // Button is pressed
      if (!b_button_pressed) {
        t_power_on_button = millis();
        b_button_pressed = true;  // Mark button as pressed
      }

      if (millis() - t_power_on_button >= 500) {
        Serial.println("Button held for 0.5 second. Powering on...");
        // Execute power on logic
        break;  // Exit loop to continue with other code
      }
    } else {
      // If the button is released
      if (b_button_pressed) {
        //Power on by button press
        Serial.println("Button released before 0.5 second.");
        Serial.println("Going to sleep now.");
        shut_down_now_nobeep();
        break;  // Exit loop to enter sleep mode
      }
      b_button_pressed = false;  // Reset mark
    }
  }
  gpio_hold_dis((gpio_num_t)PWR_CTRL);  // Disable GPIO hold mode for the specified pin, allowing it to be controlled
  pinMode(PWR_CTRL, OUTPUT);            // Set the PWR_CTRL pin as an output pin
  digitalWrite(PWR_CTRL, HIGH);         // Set the PWR_CTRL pin to HIGH, turning on the connected device or circuit
#if defined(V7_3) || defined(V7_4) || defined(V7_5) || defined(V8_0)
  gpio_hold_dis((gpio_num_t)ACC_PWR_CTRL);  // Disable GPIO hold mode for the specified pin, allowing it to be controlled
  pinMode(ACC_PWR_CTRL, OUTPUT);            // Set the PWR_CTRL pin as an output pin
  digitalWrite(ACC_PWR_CTRL, HIGH);         // Set the PWR_CTRL pin to HIGH, turning on the connected device or circuit
  Serial.println("ACC_PWR_CTRL = HIGH");
#endif
#ifdef ESP32
  Wire.begin(I2C_SDA, I2C_SCL);
#endif
#ifdef HW_SPI
  SPI.begin(OLED_SCLK, -1, OLED_SDIN, OLED_CS);
#endif
  if (b_ble_enabled) {
    ble_init();
  }
  EEPROM.begin(512);
  Serial.println("Begin!");
#ifdef ADS1115ADC
  ADS_init();
#endif
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
  analogReadResolution(ADC_BIT);
  pinMode(BUZZER, OUTPUT);
  EEPROM.get(i_addr_beep, b_beep);

  if (GPIO_power_on_with != BATTERY_CHARGING) {

    buzzer.beep(1, BUZZER_DURATION);
  }
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(255);
  u8g2.setFont(FONT_M);
  power_off(15);
#ifdef WELCOME
  EEPROM.get(i_addr_welcome, str_welcome);
  str_welcome.trim();
#endif

#ifdef ROTATION_180
  u8g2.setDisplayRotation(U8G2_R2);
#else
  u8g2.setDisplayRotation(U8G2_R0);
#endif


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
  boolean _tare = true;  //电子秤初始化去皮，如果不想去皮则设为false
  //whether the scale will tare on start.
  scale.begin();
  scale.setSamplesInUse(4);  //设置灵敏度
  scale.start(stabilizingtime, _tare);
  scale.setCalFactor(f_calibration_value);  //设置偏移量
  //set the calibration value
  //scale.setSamplesInUse(sample[i_sample]);  //设置灵敏度

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
  //EEPROM.get(i_addr_requireHeartBeat, b_requireHeartBeat);

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
  if (b_beep != 0 && b_beep != 1) {
    b_beep = 1;
    EEPROM.put(i_addr_beep, b_beep);
    EEPROM.commit();
  }
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
  // if (b_requireHeartBeat > 1 || b_requireHeartBeat < 0) {
  //   b_requireHeartBeat = true;
  //   EEPROM.put(i_addr_requireHeartBeat, b_requireHeartBeat);
  //   EEPROM.commit();
  // }

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

  // //wifiota
  // #ifdef WIFI
  if (digitalRead(BUTTON_CIRCLE) == LOW && digitalRead(BUTTON_SQUARE) == LOW) {
    //wifiOta();

    b_menu = true;
    refreshOLED((char *)"HDS Setup", FONT_EXTRACTION);
    delay(1000);
    //showMenu();
  }
  // #endif

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

  Serial.print("\tCal_Val: ");
  Serial.println(f_calibration_value);
  Serial.print("Button:\tSQARE\tCIRCLE\tPOWER");
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(BUTTON_SQUARE);
  Serial.print("\t");
  Serial.print(BUTTON_CIRCLE);
  Serial.print("\t");
  Serial.println(GPIO_NUM_BUTTON_POWER);
#ifdef ADS1232ADC
  Serial.println("Button:\tI2C_SDA\tI2C_SCK\tADC_DOUT\tADC_SCLK\tADC_PWDN\tBUZZER");
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(I2C_SDA);
  Serial.print("\t");
  Serial.print(I2C_SCL);
  Serial.print("\t");
  Serial.print(SCALE_DOUT);
  Serial.print("\t");
  Serial.print(SCALE_SCLK);
  Serial.print("\t");
  Serial.print(SCALE_PDWN);
  Serial.print("\t");
  Serial.println(BUZZER);
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
}

void pureScale() {
  static boolean newDataReady = 0;
  static boolean scaleStable = 0;
  float f_weight_adc_raw = 0;
  if (scale.update()) newDataReady = true;
  if (newDataReady) {
    f_weight_adc = scale.getData();
    //Serial.println(f_weight_adc);
    // circularBuffer[bufferIndex] = f_weight_adc;
    // bufferIndex = (bufferIndex + 1) % windowLength;
    // calculate moving average
    // f_weight_smooth = 0;
    // for (int i = 0; i < windowLength; i++) {
    //   f_weight_smooth += circularBuffer[i];
    // }
    // f_weight_smooth /= windowLength;

    // if (f_weight_smooth >= f_displayedValue - OledTolerance && f_weight_smooth <= f_displayedValue + OledTolerance) {
    //   // scale value is within tolerance range, do nothing
    //   // or weight is around 0, then set to 0.
    //   if (f_weight_smooth > -0.1 && f_weight_smooth < 0.1)
    //     f_displayedValue = 0.0;
    // } else {
    //   // scale value is outside tolerance range, update displayed value
    //   f_displayedValue = f_weight_smooth;
    //   // print result to serial monitor
    // }

    if (f_weight_adc >= -0.14 && f_weight_adc <= 0.14) {
      f_displayedValue = 0.0;
    } else {
      // scale value is outside tolerance range, update displayed value
      f_displayedValue = f_weight_adc;
      // print result to serial monitor
    }

    f_weight_before_input = f_displayedValue;

    //串口输出原始重量读数
    // Adafruit_Sensor *mpu_accel;
    // sensors_event_t a;
    // mpu_accel = mpu.getAccelerometerSensor();
    // mpu_accel->getEvent(&a);

    dtostrf(f_displayedValue, 7, i_decimal_precision, c_weight);
    if (b_weight_in_serial == true)
      Serial.println(trim(c_weight));
    newDataReady = false;
  }
  if (scale.getTareStatus()) {
    //buzzer.beep(2, 50);
    t_tareStatus = millis();
    b_weight_quick_zero = false;
  }
  //记录咖啡粉时，将重量固定为0
  if (b_weight_quick_zero || b_bootTare)
    f_displayedValue = 0.0;

  float ratio_temp = f_displayedValue / f_weight_dose;
  if (ratio_temp < 0)
    ratio_temp = 0.0;
  if (f_weight_dose < 0.1)
    ratio_temp = 0.0;
  dtostrf(ratio_temp, 7, i_decimal_precision, c_brew_ratio);
}

void serialCommand() {
  if (Serial.available()) {
    String inputString = Serial.readStringUntil('\n');
    inputString.trim();

    if (inputString.startsWith("welcome ")) {
      //strcpy(str_welcome, inputString.substring(8).c_str());
      EEPROM.put(i_addr_welcome, inputString.substring(8));
      EEPROM.commit();
    }

    if (inputString.startsWith("gyro")) {
      //strcpy(str_welcome, inputString.substring(8).c_str());
      Serial.print("\tGyro:");
      Serial.println(gyro_z());
    }

    if (inputString.startsWith("cp ")) {  //手冲粉量
      INPUTCOFFEEPOUROVER = inputString.substring(3).toFloat();
      EEPROM.put(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
      EEPROM.commit();
    }

    if (inputString.startsWith("v")) {  //电压
      Serial.print("Battery Voltage:");
      Serial.print(getVoltage(BATTERY_PIN));
#ifndef ADS1115ADC
      int adcValue = analogRead(BATTERY_PIN);                              // Read the value from ADC
      float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;  // Calculate voltage at ADC pin
      Serial.print("\tADC Voltage:");
      Serial.print(voltageAtPin);
      Serial.print("\tbatteryCalibrationFactor: ");
      Serial.print(f_batteryCalibrationFactor);
#endif
      Serial.print("\tlowBatteryCounterTotal: ");
      Serial.print(i_lowBatteryCountTotal);
    }

    if (inputString.startsWith("vf ")) {                                                 // Command to set the battery voltage calibration factor
      int adcValue = analogRead(BATTERY_PIN);                                            // Read the ADC value from the battery pin
      float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;                // Calculate the voltage at the ADC pin
      float batteryVoltage = voltageAtPin * dividerRatio;                                // Calculate the actual battery voltage using the voltage divider ratio
      f_batteryCalibrationFactor = inputString.substring(3).toFloat() / batteryVoltage;  // Calculate the calibration factor from user input
      EEPROM.put(i_addr_batteryCalibrationFactor, f_batteryCalibrationFactor);           // Store the calibration factor in EEPROM

#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();  // Commit changes to EEPROM to save the calibration factor
#endif

      Serial.print("Battery Voltage Factor set to: ");  // Output the new calibration factor to the Serial Monitor
      Serial.println(f_batteryCalibrationFactor);
    }

    if (inputString.startsWith("cv ")) {  //校准值
      f_calibration_value = inputString.substring(3).toFloat();
      EEPROM.put(i_addr_calibration_value, f_calibration_value);
      EEPROM.commit();
    }

    if (inputString.startsWith("oled ")) {                     // 校准值
      int i_oled_contrast = inputString.substring(5).toInt();  // Parse the input as an integer

      // Clamp the contrast value between 0 and 255
      i_oled_contrast = constrain(i_oled_contrast, 0, 255);

      u8g2.setContrast(i_oled_contrast);

      Serial.print("OLED contrast set to ");
      Serial.println(i_oled_contrast);
    }

    if (inputString.startsWith("oledon")) {
      u8g2.setPowerSave(0);
    }
    if (inputString.startsWith("oledoff")) {
      u8g2.setPowerSave(1);
    }

    if (inputString.startsWith("reset")) {  //重启
      reset();
    }

    if (inputString.startsWith("cal0")) {  //calibrate load cell 0
      b_calibration = true;
      i_calibration = 0;
    }

    if (inputString.startsWith("cal1")) {  ////calibrate load cell 1
      b_calibration = true;
      i_calibration = 1;
    }

    if (inputString.startsWith("ota")) {  //WiFi ota
      wifiOta();
    }

    if (inputString.startsWith("tare")) {
      buttonCircle_Pressed();
      buttonCircle_Released();
    }

    if (inputString.startsWith("set")) {
      buttonSquare_Pressed();
    }

    if (inputString.startsWith("debug ")) {
      b_debug = inputString.substring(6).toInt();  // Parse the input as an integer
      Serial.print("Debug:");
      if (b_debug)
        Serial.print("On ");
      else
        Serial.print("Off ");
      //EEPROM.put(i_addr_debug, b_debug);
      //EEPROM.commit();
    }

    if (inputString.startsWith("beep")) {  //蜂鸣器
      b_beep = !b_beep;
      EEPROM.put(i_addr_beep, b_beep);
      EEPROM.commit();
    }
    // Send the updated values via USB serial
    Serial.print("\tBuzzer:");
    if (b_beep)
      Serial.println("On");
    else
      Serial.println("Off");
  }
}


void loop() {
  if (bleState == CONNECTED && b_requireHeartBeat) {
    if (millis() - t_heartBeat > HEARTBEAT_TIMEOUT) {
      disconnectBLE();
      t_heartBeat = millis() + 10000;
    }
  }

  if (deviceConnected) {
    power_off(-1);  //reset power off timer
  } else {
    power_off(15);  //power off after 15 minutes
  }
  //serialCommand();
  if (Serial.available()) {
    uint8_t data[32];  // 假设最大接收 32 字节数据
    size_t len = 0;
    while (Serial.available() && len < sizeof(data)) {
      data[len++] = Serial.read();
    }
    MyUsbCallbacks usbCallbacks;
    usbCallbacks.onWrite(data, len);  // 调用 onWrite 函数处理串口数据
  }

  buttonCircle.check();
  buttonSquare.check();
  buzzer.check();

  if (!b_softSleep) {
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
#if defined(DEBUG) && defined(CHECKBATTERY)
    debugData();
#endif  //DEBUG
    checkBattery();
    if (b_menu) {
      showMenu();
    } else if (GPIO_power_on_with == BATTERY_CHARGING) {
      if (b_chargingOLED) {
        if (digitalRead(BATTERY_CHARGING) == LOW && !b_calibration) {
          //read voltage
          float voltage = getVoltage(BATTERY_PIN);
          float perc = map(voltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);  //map funtion doesn't take float as input.
          chargingOLED((int)perc, voltage);                                                                                   //show charging ui
        } else {
          //read voltage
          float voltage = getVoltage(BATTERY_PIN);
          if (voltage > 4.1) {
            //charging complete
            Serial.println("Charging compelete.");
          } else {
            //charging not complete, but the serial maynot be ouput cause usb unplugged.
            Serial.println("USB Unplugged, charging not compelete.");
          }
          shut_down_now_nobeep();  //deepsleep
        }
      }
    } else {
      //
      if (b_ota == true) {
        ElegantOTA.loop();
      } else if (b_calibration == true) {
        calibration(i_calibration);
      } else if (b_usbLinked == true) {
        //showing charging animation when powered off
        //charging();
      } else {
        if (b_ble_enabled)
          sendBleWeight();
        if (b_usbweight_enabled)
          sendUsbWeight();
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
#ifdef ROTATION_180
      u8g2.setDisplayRotation(U8G2_R2);
#else
      u8g2.setDisplayRotation(U8G2_R0);
#endif

      // Get the display width and height
      int16_t displayWidth = u8g2.getDisplayWidth();
      int16_t displayHeight = u8g2.getDisplayHeight();

      // Set the size of the battery outline and inner rectangle
      int16_t width = 100, height = 30;
      int16_t innerWidth = width - 2;    // Width of the inner rectangle
      int16_t innerHeight = height - 2;  // Height of the inner rectangle

      // Calculate the centered position for the battery outline and inner rectangle
      int16_t x = (displayWidth - width) / 2;
      int16_t y = (displayHeight - height) / 2 - 6;

      u8g2.setFontMode(1);
      u8g2.setDrawColor(1);
      // Draw the battery outline (rounded rectangle)
      u8g2.drawRFrame(x, y, width, height, 5);  // Outline with a corner radius of 5

      // Calculate the height for the inner rectangle based on percentage
      int16_t innerY = y + 1 + innerHeight * (1 - (perc / 100.0));  // Y position of the inner rectangle
      //u8g2.drawBox(x + 1, y + 1, innerWidth * (perc / 100.0), innerHeight);  // Inner rectangle, filled to show charge level
      u8g2.drawVLine(x + width + 2, y + 7, height - 7 * 2);
      for (int i = 0; i < innerWidth - 1; i = i + 2) {
        if (i < (innerWidth * (perc / 100.0))) {
          u8g2.drawVLine(x + 2 + i, y + 2, innerHeight - 2);
        }
      }

      u8g2.setDrawColor(2);
      // Display the battery percentage
      char batteryText[10];
      snprintf(batteryText, sizeof(batteryText), "%d%%", (perc > 100) ? 100 : perc);
      if (perc > 100) {
        strcat(batteryText, "+");
      }
      u8g2.setFont(u8g2_font_ncenB12_tr);                                                                 // Set font
      u8g2.drawStr(x + (width / 4) - (u8g2.getStrWidth(batteryText) / 2), y + height + 15, batteryText);  // Center the percentage text

      // Display the voltage
      char voltageText[10];
      snprintf(voltageText, sizeof(voltageText), "%.1fV", voltage);
      u8g2.drawStr(x + (width / 4) + (width / 2) - (u8g2.getStrWidth(voltageText) / 2), y + height + 15, voltageText);  // Center the voltage text

    } while (u8g2.nextPage());
  }
}


void updateOled() {
  if (millis() > t_oled_refresh + i_oled_print_interval) {
    //达到设定的oled刷新频率后进行刷新
    t_oled_refresh = millis();

    u8g2.firstPage();
    do {
#ifdef ROTATION_180
      u8g2.setDisplayRotation(U8G2_R2);
#else
      u8g2.setDisplayRotation(U8G2_R0);
#endif
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
      drawTare();
      drawShutdownFail();
      drawAbout();
      drawDebug();

    } while (u8g2.nextPage());
  }
}

void drawShutdownFail() {
  if (b_shutdownFailBle && millis() - t_shutdownFailBle < 3000) {
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
  if (input <= -1000.0)
    gramWidth = 0;
  int x_integer = (128 - (integerWidth + decimalWidth + gramWidth + decimalPointWidth - 6 - 1)) / 2;
  int x_decimalPoint = x_integer + integerWidth - 4;
  int x_decimal = x_decimalPoint + decimalPointWidth - 4;
  int x_gram = x_decimal + decimalWidth - 1;
  int y = AT() - 15;
  if (String(sec2sec(stopWatch.elapsed())) == "0s")
    y = AT();
  u8g2.drawStr(x_decimalPoint, y, ".");
  u8g2.drawStr(x_integer, y, trim(integerStr));  // Assuming vertical position at 28, adjust as needed
  u8g2.drawStr(x_decimal, y, trim(decimalStr));
  if (input > -1000.0) {
    u8g2.setFont(FONT_GRAM);
    u8g2.drawStr(x_gram, y - 5, "g");
  }
}

void drawTime() {
  if (String(sec2sec(stopWatch.elapsed())) != "0s") {
    u8g2.setFont(FONT_TIMER);
    u8g2.drawStr(AC(sec2sec(stopWatch.elapsed())), LCDHeight - 8, sec2sec(stopWatch.elapsed()));
  }
}

//button pressed box, left and right, added xor
void drawButton() {
  if (digitalRead(BUTTON_CIRCLE) == LOW) {
    // u8g2.drawBox(0, 0, 10, 64);
    u8g2.drawXBM(0, 0, 15, 16, image_circle);
  }
  if (digitalRead(BUTTON_SQUARE) == LOW)
    //u8g2.drawBox(118, 0, 118, 64);
    u8g2.drawXBM(114, 0, 14, 16, image_square);
}

//some quick variable
long t_ble_box = 0;
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

void drawBattery() {
  if (millis() - t_batteryIcon >= 500) {  // Toggle every 500 ms
    t_batteryIcon = millis();
    b_showBatteryIcon = !b_showBatteryIcon;
  }
#if defined(V7_4) || defined(V7_5) || defined(V8_0)
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
    int i_batteryPercent = map(getVoltage(BATTERY_PIN) * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);
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
    float voltage = getVoltage(BATTERY_PIN);
    int perc = map(voltage * 1000, showEmptyBatteryBelowVoltage * 1000, showFullBatteryAboveVoltage * 1000, 0, 100);  //map funtion doesn't take float as input.
    snprintf(batteryText, sizeof(batteryText), "%d%%", (perc > 100) ? 100 : perc);

    char voltageText[10];
    if (perc > 100) {
      strcat(batteryText, "+");
    }
    snprintf(voltageText, sizeof(voltageText), "%.1fV", voltage);

    char gpioText[10];
    snprintf(gpioText, sizeof(gpioText), "GPIO:%d", i_wakeupPin);

    char gyroText[10];
    snprintf(gyroText, sizeof(gyroText), "Gyro:%.1f", gyro_z());

    char weightText[10];
    snprintf(weightText, sizeof(weightText), "%.1fg", f_displayedValue);

    //display
    u8g2.setFont(u8g2_font_6x13_tr);
    int lineHeight = 12;
    u8g2.drawStr(-54, lineHeight, LINE1);
    u8g2.drawStr(0, lineHeight * 2, (char *)trim(gpioText));
    u8g2.drawStr(0, lineHeight * 3, (char *)trim(gyroText));
    u8g2.drawStr(0, lineHeight * 4, (char *)trim(chargingText));
    u8g2.drawStr(14, lineHeight * 5, (char *)trim(bleText));

    u8g2.drawStr(55, lineHeight, (char *)trim(batteryText));
    u8g2.drawStr(55, lineHeight * 2, (char *)trim(voltageText));

    u8g2.drawStr(AR((char *)trim(weightText)), lineHeight, (char *)trim(weightText));
    u8g2.drawStr(AR(sec2sec(stopWatch.elapsed())), lineHeight * 2, sec2sec(stopWatch.elapsed()));

    // char c_bat[10] = "";
    // dtostrf(getVoltage(BATTERY_PIN), 7, 2, c_bat);
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