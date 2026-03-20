#ifndef USBCOMM_H
#define USBCOMM_H

//functions
void sendUsbVoltage();
void sendUsbLedResponse();
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendUsbGyro();
#endif

class MyUsbCallbacks {
public:
  // Function pointer members
  void (*setStableOutputThreshold)(float);
  void (*setTrackingThreshold)(float);
  void (*setTrackingUpdateInterval)(float);
  void (*buttonSquare_Pressed)();
  void (*buttonCircle_Pressed)();

  uint8_t calculateChecksum(uint8_t *data, size_t len) {
    uint8_t xorSum = 0;
    // 遍历数据中的每个字节，排除最后一个字节（假设它是校验和）
    for (size_t i = 0; i < len - 1; i++) {
      xorSum ^= data[i];
    }
    return xorSum;
  }

  // 校验数据的校验和
  bool validateChecksum(uint8_t *data, size_t len) {
    if (len < 2) {  // 至少需要 1 字节数据和 1 字节校验和
      return false;
    }
    uint8_t expectedChecksum = data[len - 1];
    uint8_t calculatedChecksum = calculateChecksum(data, len);
    return expectedChecksum == calculatedChecksum;
  }

  void onWrite(uint8_t *data, size_t len) {
    //this is what the esp32 received via usb
    Serial.print("Timer ");
    Serial.print(millis());
    Serial.print(" onWrite counter:");
    Serial.print(i_onWrite_counter++);
    Serial.print(" ");

    
    if (data == nullptr || len <= 0) {
      return;
    }


    // 打印接收到的 HEX 数据
    Serial.print("Received HEX: ");
    for (size_t i = 0; i < len; i++) {
      if (data[i] < 0x10) {  // 如果字节小于 0x10
        Serial.print("0");   // 打印前导零
      }
      Serial.print(data[i], HEX);  // 以 HEX 格式打印字节
    }
    Serial.println(" ");

    if (data[0] != 0x03) {
      String input = String((char *)data);
      handleStringCommand(input);
      return;
    }
    //check if it's a decent scale message
    if (data[1] == 0x0F) {
      //taring
      if (validateChecksum(data, len)) {
        Serial.println("Valid checksum for tare operation. Taring");
      } else {
        Serial.println("Invalid checksum for tare operation.");
      }
      b_tareByBle = true;
      t_tareByBle = millis();
      if (data[5] == 0x00) {
        /*
        Tare the scale by sending "030F000000000C" (old version, disables heartbeat)
        Tare the scale by sending "030F000000010D" (new version, leaves heartbeat as set)
        */
        b_requireHeartBeat = false;
        Serial.println("*** Heartbeat detection Off ***");
      }
      if (data[5] == 0x01) {
        /*
        Tare the scale by sending "030F000000000C" (old version, disables heartbeat)
        Tare the scale by sending "030F000000010D" (new version, leaves heartbeat as set)
        */
        Serial.print("*** Heartbeat detection remained ");
        if (b_requireHeartBeat)
          Serial.print("On");
        else
          Serial.print("Off");
        Serial.println(" ***");
      }
    } else if (data[1] == 0x0A) {
      if (data[2] == 0x00) {
        Serial.println("LED off detected. Turn off OLED.");
        u8g2.setPowerSave(1);
        b_u8g2Sleep = true;
        sendUsbLedResponse();
      } else if (data[2] == 0x01) {
        Serial.print("LED on detected. Turn on OLED.");
        u8g2.setPowerSave(0);
        b_u8g2Sleep = false;
        sendUsbLedResponse();
        if (data[5] == 0x00) {
          b_requireHeartBeat = false;
          Serial.println(" *** Heartbeat detection Off ***");
        }
        if (data[5] == 0x01) {
          Serial.print("*** Heartbeat detection remained ");
          if (b_requireHeartBeat)
            Serial.print("On");
          else
            Serial.print("Off");
          Serial.println(" ***");
        }
      } else if (data[2] == 0x02) {
        Serial.println("Power off detected.");
        b_powerOff = true;
      } else if (data[2] == 0x03) {
        if (data[3] == 0x01) {
          Serial.println("Start Low Power Mode.");
          u8g2.setContrast(0);
        } else if (data[3] == 0x00) {
          Serial.println("Exit low power mode.");
          u8g2.setContrast(255);
        }
      } else if (data[2] == 0x04) {
        if (data[3] == 0x01) {
          Serial.println("Start Soft Sleep.");
          u8g2.setPowerSave(1);
          b_softSleep = true;
          digitalWrite(PWR_CTRL, LOW);
//#if defined(ACC_MPU6050) || defined(ACC_BMA400)
          digitalWrite(ACC_PWR_CTRL, LOW);
//#endif
        } else if (data[3] == 0x00) {
          Serial.println("Exit Soft Sleep.");
          digitalWrite(PWR_CTRL, HIGH);
//#if defined(ACC_MPU6050) || defined(ACC_BMA400)
          digitalWrite(ACC_PWR_CTRL, HIGH);
//#endif
          u8g2.setPowerSave(0);
          b_softSleep = false;
        }
      }
    } else if (data[1] == 0x0B) {
      if (data[2] == 0x03) {
        Serial.println("Timer start detected.");
        stopWatch.reset();
        stopWatch.start();
      } else if (data[2] == 0x00) {
        Serial.println("Timer stop detected.");
        stopWatch.stop();
      } else if (data[2] == 0x02) {
        Serial.println("Timer zero detected.");
        stopWatch.reset();
      }
    } else if (data[1] == 0x1A) {
      if (data[2] == 0x00) {
        Serial.println("Manual Calibration via BLE");
        i_button_cal_status = 1;
        i_calibration = 0;
        b_calibration = true;
      } else if (data[2] == 0x01) {
        Serial.println("Smart Calibration via BLE");
        i_button_cal_status = 1;
        i_calibration = 1;
        b_calibration = true;
      }
    } else if (data[1] == 0x1B) {
      Serial.println("Start WiFi OTA");
      wifiUpdate();
    }
#ifdef BUZZER
    else if (data[1] == 0x1C) {  //buzzer settings
      if (data[2] == 0x00) {
        Serial.println("Buzzer Off");
        b_beep = false;  // won't store into eeprom
      } else if (data[2] == 0x01) {
        Serial.println("Buzzer On");
        b_beep = true;  // won't store into eeprom
      } else if (data[2] == 0x02) {
        Serial.println("Buzzer Beep");
        buzzer.beep(1, 50);
      }
    }
#endif
    else if (data[1] == 0x1D) {  //Sample settings
      if (data[2] == 0x00) {
        scale.setSamplesInUse(1);
        Serial.print("Samples in use set to: ");
        Serial.println(scale.getSamplesInUse());
      } else if (data[2] == 0x01) {
        scale.setSamplesInUse(2);
        Serial.print("Samples in use set to: ");
        Serial.println(scale.getSamplesInUse());
      } else if (data[2] == 0x03) {
        scale.setSamplesInUse(4);
        Serial.print("Samples in use set to: ");
        Serial.println(scale.getSamplesInUse());
      }
    } else if (data[1] == 0x1E) {
      if (data[2] == 0x00) {
        //menu control
        if (data[3] == 0x00) {
          //hide menu
          Serial.println("Hide menu");
          b_menu = false;
        } else if (data[3] == 0x01) {
          //show menu
          Serial.println("Show menu");
          b_menu = true;
        }
      } else if (data[2] == 0x01) {
        //about info
        if (data[3] == 0x00) {
          //hide about info
          Serial.println("Hide about info");
          if (b_menu)
            b_showAbout = false;  //hide about info(code in menu) if it's enabled via menu
          else
            b_about = false;  //hide about info(code in loop) if it's enabled via ble command
        } else if (data[3] == 0x01) {
          //show about info
          Serial.println("Show about info");
          b_debug = false;
          b_about = true;
          b_menu = false;
        }
      } else if (data[2] == 0x02) {
        //debug info
        if (data[3] == 0x00) {
          //hide debug info
          Serial.println("Hide debug info");
          b_debug = false;
        } else if (data[3] == 0x01) {
          //show debug info
          Serial.println("Show debug info");
          b_about = false;
          b_debug = true;
          b_menu = false;
        }
      }
    } else if (data[1] == 0x1F) {
      reset();
    } else if (data[1] == 0x20) {
      if (data[2] == 0x00) {
        Serial.println("Weight by USB disabled");
        b_usbweight_enabled = false;
      } else if (data[2] == 0x01) {
        Serial.println("Weight by USB enabled");
        b_usbweight_enabled = true;
        if (len >= 4) {
          uint16_t interval = 100;
          uint8_t multiplier = data[3];
          if (multiplier < 1) multiplier = 1;
          if (multiplier > 50) multiplier = 50;  // 最大支持 5000ms

          interval = multiplier * 100;
          weightUsbNotifyInterval = interval;

          Serial.print("USB weight interval set to ");
          Serial.print(weightUsbNotifyInterval);
          Serial.println(" ms");
        }
      }

    }
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    else if (data[1] == 0x21) {
      sendUsbGyro();
    }
#endif
    else if (data[1] == 0x22) {
      sendUsbVoltage();
    }
  }
    
  


  void handleStringCommand(String inputString) {
    if (inputString.startsWith("welcome ")) {
      //strcpy(str_welcome, inputString.substring(8).c_str());
      EEPROM.put(i_addr_welcome, inputString.substring(8));
      EEPROM.commit();
    }
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    if (inputString.startsWith("gyro")) {
      //strcpy(str_welcome, inputString.substring(8).c_str());
      Serial.print("\tGyro:");
      Serial.println(gyro_z());
    }
#endif

    if (inputString.startsWith("cp ")) {  //手冲粉量
      INPUTCOFFEEPOUROVER = inputString.substring(3).toFloat();
      EEPROM.put(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
      EEPROM.commit();
    }

    if (inputString.startsWith("v")) {  //电压
      Serial.print("Battery Voltage:");
      Serial.print(f_batteryVoltage);
      //#ifndef ADS1115ADC
      if (b_ads1115InitFail) {
        int adcValue = analogRead(BATTERY_PIN);                              // Read the value from ADC
        float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;  // Calculate voltage at ADC pin
        Serial.print("\tADC Voltage:");
        Serial.print(voltageAtPin);
        Serial.print("\tbatteryCalibrationFactor: ");
        Serial.print(f_batteryCalibrationFactor);
      }
      //#endif
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

    if (inputString.startsWith("sot ")) {
      if (setStableOutputThreshold != nullptr) {
        setStableOutputThreshold(inputString.substring(4).toFloat());
      }
      //b_tempDisablePowerOff = true;
    }

    if (inputString.startsWith("tt ")) {
      if (setTrackingThreshold != nullptr) {
        setTrackingThreshold(inputString.substring(3).toFloat());
      }
      //b_tempDisablePowerOff = true;
    }

    if (inputString.startsWith("tui ")) {
      if (setTrackingUpdateInterval != nullptr) {
        setTrackingUpdateInterval(inputString.substring(4).toFloat());
      }
      //b_tempDisablePowerOff = true;
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

    if (inputString.startsWith("tare")) {
      if (buttonCircle_Pressed != NULL) {
        buttonCircle_Pressed();
      }
    }

    if (inputString.startsWith("set")) {
      if (buttonSquare_Pressed != NULL) {
        buttonSquare_Pressed();
      }
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
#ifdef BUZZER
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
#endif
  }
};

// Send voltage via USB
void sendUsbVoltage() {
  byte data[7];
  buildVoltagePacket(data);
  Serial.write(data, 7);
}

// Send heartbeat via USB
void sendUsbHeartBeat() {
  byte data[7];
  buildHeartBeatPacket(data);
  Serial.write(data, 7);
}

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendUsbGyro() {
  byte data[7];
  buildGyroPacket(data);
  Serial.write(data, 7);
}
#endif

void sendUsbWeight() {
  byte data[7];
  buildWeightPacket(data);
  Serial.write(data, 7);
}

void sendUsbTextWeight() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastWeightTextNotifyTime >= weightTextNotifyInterval) {
    // Save the last time you sent the weight notification
    lastWeightTextNotifyTime = currentMillis;
    Serial.print(lastWeightTextNotifyTime);
    Serial.print(" Weight: ");  // 7 bytes of data
    Serial.println(f_displayedValue);
  }
}

void sendUsbButton(int buttonNumber, int buttonShortPress) {
  byte data[7];
  buildButtonPacket(data, buttonNumber, buttonShortPress);
  Serial.write(data, 7);
}

void sendUsbLedResponse() {
  byte data[7];
  buildLedResponsePacket(data);
  Serial.write(data, 7);
}
#endif


