#ifndef USBCOMM_H
#define USBCOMM_H

#include "ADS1232_ADC.h"

// Forward declaration of scale object (defined in declare.h)
extern ADS1232_ADC scale;

//functions
void sendUsbVoltage();
void sendUsbLedResponse();
void sendUsbAdsDebug();
void sendUsbAdsResetResponse(uint8_t mode, uint8_t status);
void handleAdsReset(uint8_t mode);
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

  bool requireLength(size_t actual, size_t required, const char *commandName) {
    if (actual >= required) {
      return true;
    }
    Serial.print("Ignoring short USB packet for ");
    Serial.print(commandName);
    Serial.print(": ");
    Serial.print(actual);
    Serial.print("/");
    Serial.println(required);
    return false;
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
      String input;
      input.reserve(len);
      for (size_t i = 0; i < len; i++) {
        input += (char)data[i];
      }
      handleStringCommand(input);
      return;
    }
    if (!requireLength(len, 2, "message header")) {
      return;
    }
    //check if it's a decent scale message
    if (data[1] == 0x0F) {
      if (!requireLength(len, 7, "tare")) {
        return;
      }
      //taring
      if (validateChecksum(data, len)) {
        Serial.println("Valid checksum for tare operation. Taring");
      } else {
        Serial.println("Invalid checksum for tare operation.");
        return;
      }
      requestRemoteTare();
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
      if (!requireLength(len, 3, "LED/power")) {
        return;
      }
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
        if (!requireLength(len, 6, "LED on")) {
          return;
        }
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
        if (!requireLength(len, 4, "low power")) {
          return;
        }
        if (data[3] == 0x01) {
          Serial.println("Start Low Power Mode.");
          u8g2.setContrast(0);
        } else if (data[3] == 0x00) {
          Serial.println("Exit low power mode.");
          u8g2.setContrast(255);
        }
      } else if (data[2] == 0x04) {
        if (!requireLength(len, 4, "soft sleep")) {
          return;
        }
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
      if (!requireLength(len, 3, "timer")) {
        return;
      }
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
      if (!requireLength(len, 3, "calibration")) {
        return;
      }
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
      if (!requireLength(len, 3, "buzzer")) {
        return;
      }
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
      if (!requireLength(len, 3, "sample settings")) {
        return;
      }
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
      if (!requireLength(len, 4, "menu/about/debug")) {
        return;
      }
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
      if (!requireLength(len, 3, "USB weight")) {
        return;
      }
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
    else if (data[1] == 0x25) {
      if (!requireLength(len, 3, "ADS debug")) {
        return;
      }
      // ADS1232 Debug commands
      if (data[2] == 0x00) {
        Serial.println("ADS debug off via hex");
        scale.setDebugEnabled(false);
      } else if (data[2] == 0x01) {
        Serial.println("ADS debug on via hex");
        scale.setDebugEnabled(true);
      } else if (data[2] == 0x02) {
        Serial.println("ADS debug info via hex");
        sendUsbAdsDebug();
      }
    }
    else if (data[1] == 0x26) {
      if (!requireLength(len, 3, "ADS reset")) {
        return;
      }
      // ADS1232 Reset command
      if (data[2] <= 0x02) {
        handleAdsReset(data[2]);
      } else {
        Serial.print("ADS reset: unknown mode 0x");
        Serial.println(data[2], HEX);
      }
    }
  }
    
  


  void handleStringCommand(String inputString) {
    inputString.trim(); // Remove leading/trailing whitespace
    Serial.printf("handling %s\n", inputString.c_str());
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
    
    if (inputString.startsWith("adsd ")) {
      String cmd = inputString.substring(5);
      cmd.trim(); // Remove any whitespace/newlines
      if (cmd == "on" || cmd == "1") {
        scale.setDebugEnabled(true);
        Serial.println("ADS1232 debug enabled");
      } else if (cmd == "off" || cmd == "0") {
        scale.setDebugEnabled(false);
        Serial.println("ADS1232 debug disabled");
      } else if (cmd == "info") {
        // Get one-time debug snapshot without enabling continuous mode
        ADS1232DebugInfo info = scale.getDebugInfo();
        Serial.println("=== ADS1232 Debug Snapshot ===");
        Serial.print("Raw: "); Serial.print(info.rawValue);
        Serial.print(" | Smooth: "); Serial.print(info.smoothedValue);
        Serial.print(" | Tare: "); Serial.println(info.tareOffset);
        Serial.print("SPS: "); Serial.print(info.sps, 2);
        Serial.print(" | StdDev: "); Serial.println(info.dataStdDev, 2);
        Serial.print("Range: "); Serial.print(info.dataMin);
        Serial.print(" to "); Serial.println(info.dataMax);
        Serial.println("==============================");
      }
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

// Build ADS1232 debug packet
// Packet format (39 bytes total):
// [0]    = 0x03 (model byte)
// [1]    = 0x25 (debug packet type)
// [2-5]  = timestamp (4 bytes, unsigned long)
// [6-9]  = rawValue (4 bytes, signed long)
// [10-13] = smoothedValue (4 bytes, signed long)
// [14-17] = tareOffset (4 bytes, signed long)
// [18-19] = conversionTime (2 bytes, float * 100 to get 0.01ms precision)
// [20-21] = sps (2 bytes, float * 100)
// [22]    = readIndex (1 byte)
// [23]    = samplesInUse (1 byte)
// [24-27] = dataMin (4 bytes, signed long)
// [28-31] = dataMax (4 bytes, signed long)
// [32-35] = dataAvg (4 bytes, signed long)
// [36-37] = dataStdDev (2 bytes, float * 10)
// [38]    = flags (bits: 0=dataOutOfRange, 1=signalTimeout, 2=tareInProgress)
// [39]    = tareTimes (1 byte)
// [40]    = checksum (XOR of bytes 0-39)
void buildAdsDebugPacket(byte data[41]) {
  ADS1232DebugInfo info = scale.getDebugInfo();
  
  data[0] = 0x03;  // model byte
  data[1] = 0x25;  // debug packet type
  
  // Timestamp (4 bytes)
  data[2] = (info.timestamp >> 24) & 0xFF;
  data[3] = (info.timestamp >> 16) & 0xFF;
  data[4] = (info.timestamp >> 8) & 0xFF;
  data[5] = info.timestamp & 0xFF;
  
  // Raw value (4 bytes, signed)
  data[6] = (info.rawValue >> 24) & 0xFF;
  data[7] = (info.rawValue >> 16) & 0xFF;
  data[8] = (info.rawValue >> 8) & 0xFF;
  data[9] = info.rawValue & 0xFF;
  
  // Smoothed value (4 bytes, signed)
  data[10] = (info.smoothedValue >> 24) & 0xFF;
  data[11] = (info.smoothedValue >> 16) & 0xFF;
  data[12] = (info.smoothedValue >> 8) & 0xFF;
  data[13] = info.smoothedValue & 0xFF;
  
  // Tare offset (4 bytes, signed)
  data[14] = (info.tareOffset >> 24) & 0xFF;
  data[15] = (info.tareOffset >> 16) & 0xFF;
  data[16] = (info.tareOffset >> 8) & 0xFF;
  data[17] = info.tareOffset & 0xFF;
  
  // Conversion time (2 bytes, float * 100 for 0.01ms precision)
  uint16_t convTime = (uint16_t)(info.conversionTime * 100);
  data[18] = (convTime >> 8) & 0xFF;
  data[19] = convTime & 0xFF;
  
  // SPS (2 bytes, float * 100)
  uint16_t sps = (uint16_t)(info.sps * 100);
  data[20] = (sps >> 8) & 0xFF;
  data[21] = sps & 0xFF;
  
  // Read index (1 byte)
  data[22] = info.readIndex & 0xFF;
  
  // Samples in use (1 byte)
  data[23] = info.samplesInUse & 0xFF;
  
  // Data min (4 bytes, signed)
  data[24] = (info.dataMin >> 24) & 0xFF;
  data[25] = (info.dataMin >> 16) & 0xFF;
  data[26] = (info.dataMin >> 8) & 0xFF;
  data[27] = info.dataMin & 0xFF;
  
  // Data max (4 bytes, signed)
  data[28] = (info.dataMax >> 24) & 0xFF;
  data[29] = (info.dataMax >> 16) & 0xFF;
  data[30] = (info.dataMax >> 8) & 0xFF;
  data[31] = info.dataMax & 0xFF;
  
  // Data avg (4 bytes, signed)
  data[32] = (info.dataAvg >> 24) & 0xFF;
  data[33] = (info.dataAvg >> 16) & 0xFF;
  data[34] = (info.dataAvg >> 8) & 0xFF;
  data[35] = info.dataAvg & 0xFF;
  
  // StdDev (2 bytes, float * 10 for 0.1 precision)
  uint16_t stdDev = (uint16_t)(info.dataStdDev * 10);
  data[36] = (stdDev >> 8) & 0xFF;
  data[37] = stdDev & 0xFF;
  
  // Flags (1 byte: bit 0=dataOutOfRange, bit 1=signalTimeout, bit 2=tareInProgress)
  data[38] = (info.dataOutOfRange ? 0x01 : 0x00) |
             (info.signalTimeout ? 0x02 : 0x00) |
             (info.tareInProgress ? 0x04 : 0x00);
  
  // Tare times (1 byte)
  data[39] = info.tareTimes & 0xFF;
  
  // Checksum (XOR of all previous bytes)
  byte checksum = 0;
  for (int i = 0; i < 40; i++) {
    checksum ^= data[i];
  }
  data[40] = checksum;
}

// Build ADS1232 reset response packet (5 bytes)
// [0] = 0x03 (model), [1] = 0x26 (reset response), [2] = mode, [3] = status, [4] = checksum
void buildAdsResetResponsePacket(byte data[5], uint8_t mode, uint8_t status) {
  data[0] = 0x03;
  data[1] = 0x26;
  data[2] = mode;
  data[3] = status;
  // Checksum: XOR of bytes 0-3
  data[4] = data[0] ^ data[1] ^ data[2] ^ data[3];
}

void sendUsbAdsResetResponse(uint8_t mode, uint8_t status) {
  byte data[5];
  buildAdsResetResponsePacket(data, mode, status);
  Serial.write(data, 5);
}

// Handle ADS1232 reset command (0x26)
// mode 0x00: soft reset (powerDown/powerUp only)
// mode 0x01: reset + blocking refreshDataSet()
// mode 0x02: reset + refreshDataSet() + tare
void handleAdsReset(uint8_t mode) {
  Serial.print("ADS reset mode 0x0");
  Serial.println(mode);

  // Step 1: Power cycle the ADS1232
  scale.powerDown();
  delay(500);
  scale.powerUp();
  resetAdcRecoveryState();

  // Step 2: Check if ADS came back (DOUT should go low when conversion ready)
  unsigned long startTime = millis();
  bool adsAlive = false;
  while (millis() - startTime < 500) {
    if (digitalRead(scale.getDoutPin()) == LOW) {
      adsAlive = true;
      break;
    }
    yield();
  }

  if (!adsAlive) {
    Serial.println("ADS reset FAILED: DOUT timeout");
    sendUsbAdsResetResponse(mode, 0x01);
    return;
  }

  Serial.println("ADS reset: DOUT went low, ADC alive");

  // Step 3: Refresh dataset if mode >= 0x01
  if (mode >= 0x01) {
    if (!scale.refreshDataSet()) {
      Serial.println("ADS reset FAILED: dataset refresh timeout");
      sendUsbAdsResetResponse(mode, 0x02);
      return;
    }
    Serial.println("ADS reset: dataset refreshed");
  }

  // Step 4: Tare + reset compensations if mode == 0x02
  if (mode == 0x02) {
    requestRemoteTare();
    Serial.println("ADS reset: tare requested");
  }

  sendUsbAdsResetResponse(mode, 0x00);
  Serial.println("ADS reset complete");
}

// Send ADS debug info via USB
void sendUsbAdsDebug() {
  byte data[41];
  buildAdsDebugPacket(data);
  Serial.write(data, 41);
  
  // Also print human-readable version to Serial
  Serial.println("ADS Debug packet sent (41 bytes)");
}

#endif

