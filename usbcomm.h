#ifndef USBCOMM_H
#define USBCOMM_H

//functions
void sendUsbVoltage();
void sendUsbGyro();

class MyUsbCallbacks {
public:
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


    if (data != nullptr && len > 0) {
      // 打印接收到的 HEX 数据
      Serial.print("Received HEX: ");
      for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) {  // 如果字节小于 0x10
          Serial.print("0");   // 打印前导零
        }
        Serial.print(data[i], HEX);  // 以 HEX 格式打印字节
      }
      Serial.print(" ");

      if (data[0] == 0x03) {
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
        } else if (data[1] == 0x0A) {
          if (data[2] == 0x00) {
            Serial.print("LED off detected. Turn off OLED.");
            u8g2.setPowerSave(1);
            b_u8g2Sleep = true;
            if (data[5] == 0x00) {
              b_requireHeartBeat = false;
              Serial.print(" *** Heartbeat detection Off ***");
            }
            if (data[5] == 0x01) {
              b_requireHeartBeat = true;
              Serial.print(" *** Heartbeat detection On ***");
            }
            Serial.println();
          } else if (data[2] == 0x01) {
            Serial.print("LED on detected. Turn on OLED.");
            u8g2.setPowerSave(0);
            b_u8g2Sleep = false;
            if (data[5] == 0x00) {
              b_requireHeartBeat = false;
              Serial.print(" *** Heartbeat detection Off ***");
            }
            if (data[5] == 0x01) {
              b_requireHeartBeat = true;
              Serial.print(" *** Heartbeat detection On ***");
            }
            Serial.println();
          } else if (data[2] == 0x02) {
            Serial.println("Power off detected.");
            shut_down_now_nobeep();
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
              digitalWrite(ACC_PWR_CTRL, LOW);
            } else if (data[3] == 0x00) {
              Serial.println("Exit Soft Sleep.");
              digitalWrite(PWR_CTRL, HIGH);
              digitalWrite(ACC_PWR_CTRL, HIGH);
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
        } else if (data[1] == 0x1C) {  //buzzer settings
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
        } else if (data[1] == 0x1D) {  //Sample settings
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
          }
        } else if (data[1] == 0x21) {
          sendUsbGyro();
        } else if (data[1] == 0x22) {
          sendUsbVoltage();
        }
      }
    }
  }
};

void sendUsbVoltage() {
  Serial.print("Battery Voltage:");
  float voltage = getVoltage(BATTERY_PIN);
  Serial.print(voltage);
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

  byte data[7];
  // float weight = scale.getData();
  byte voltageByte1, voltageByte2;

  encodeWeight(voltage, voltageByte1, voltageByte2);

  data[0] = modelByte;
  data[1] = 0x22;  // Type byte for gyro data
  data[2] = voltageByte1;
  data[3] = voltageByte2;
  // Fill the rest with dummy data or real data as needed
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

  // Use Serial.write to send data via USB (serial)
  Serial.write(data, 7);  // 7 bytes of data
}

void sendUsbGyro() {
  byte data[7];
  // float weight = scale.getData();
  float gyro = gyro_z();
  byte gyroByte1, gyroByte2;

  encodeWeight(gyro, gyroByte1, gyroByte2);

  data[0] = modelByte;
  data[1] = 0x21;  // Type byte for gyro data
  data[2] = gyroByte1;
  data[3] = gyroByte2;
  // Fill the rest with dummy data or real data as needed
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

  // Use Serial.write to send data via USB (serial)
  Serial.write(data, 7);  // 7 bytes of data
}

void sendUsbWeight() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastWeightNotifyTime >= weightNotifyInterval) {
    // Save the last time you sent the weight notification
    lastWeightNotifyTime = currentMillis;

    byte data[7];
    // float weight = scale.getData();
    float weight = f_displayedValue;
    byte weightByte1, weightByte2;

    encodeWeight(weight, weightByte1, weightByte2);

    data[0] = modelByte;
    data[1] = 0xCE;  // Type byte for weight stable
    data[2] = weightByte1;
    data[3] = weightByte2;
    // Fill the rest with dummy data or real data as needed
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

    // Use Serial.write to send data via USB (serial)
    Serial.write(data, 7);  // 7 bytes of data
  }
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
  // buttonNumber 1 for button O, 2 for button[]
  // buttonShortPress 1 for short press, 2 for long press
  byte data[7];

  data[0] = modelByte;
  data[1] = 0xAA;  // Type byte for button press
  data[2] = buttonNumber;
  data[3] = buttonShortPress;
  // Fill the rest with dummy data or real data as needed
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

  // Use Serial.write to send data via USB (serial)
  Serial.write(data, 7);  // 7 bytes of data
}
#endif