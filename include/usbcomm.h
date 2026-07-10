#ifndef USBCOMM_H
#define USBCOMM_H

#include "ADS1232_ADC.h"
#include "decent_protocol.h"
#include <math.h>
#include <string.h>

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

static inline uint16_t encodeScaledUInt16(float value, float scale) {
  float scaled = value * scale;
  if (!isfinite(scaled) || scaled <= 0.0f) {
    return 0;
  }
  if (scaled > 65535.0f) {
    return 65535;
  }
  return (uint16_t)scaled;
}

struct UsbDecentCommandSink {
  const char *transportName() {
    return "USB";
  }

  void requestTare() {
    requestRemoteTare();
  }

  void displayOff() {
    u8g2.setPowerSave(1);
    b_u8g2Sleep = true;
    sendUsbLedResponse();
  }

  void displayOn() {
    u8g2.setPowerSave(0);
    b_u8g2Sleep = false;
    sendUsbLedResponse();
  }

  void powerOff() {
    b_powerOff = true;
  }

  void lowPowerOn() {
    u8g2.setContrast(0);
  }

  void lowPowerOff() {
    u8g2.setContrast(255);
  }

  void softSleepOn() {
    u8g2.setPowerSave(1);
    b_softSleep = true;
    digitalWrite(PWR_CTRL, LOW);
    digitalWrite(ACC_PWR_CTRL, LOW);
  }

  void softSleepOff() {
    if (b_softSleep) {
      wakeScaleFromSoftSleep("USB soft wake");
    } else {
      u8g2.setPowerSave(0);
      b_u8g2Sleep = false;
    }
  }

  void timerStart() {
    stopWatch.reset();
    stopWatch.start();
  }

  void timerStop() {
    stopWatch.stop();
  }

  void timerZero() {
    stopWatch.reset();
  }

  void wifiUpdate() {
    Serial.println("Start WiFi OTA");
    ::wifiUpdate();
  }

#ifdef BUZZER
  void buzzerOff() {
    b_beep = false;
  }

  void buzzerOn() {
    b_beep = true;
  }

  void buzzerBeep() {
    buzzer.beep(1, 50);
  }
#endif

  void setSamplesInUse(uint8_t samplesInUse) {
    if (setScaleSamplesInUseWhenReady(samplesInUse, "USB samples")) {
      Serial.print("Samples in use set to: ");
      Serial.println(scale.getSamplesInUse());
    } else {
      Serial.println("Samples in use refresh failed");
    }
  }

  void reset() {
    ::reset();
  }

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
  void sendGyro() {
    sendUsbGyro();
  }
#endif

  void sendVoltage() {
    sendUsbVoltage();
  }

  void adsDebug(uint8_t mode) {
    if (mode == 0x00) {
      Serial.println("ADS debug off via hex");
      scale.setDebugEnabled(false);
    } else if (mode == 0x01) {
      Serial.println("ADS debug on via hex");
      scale.setDebugEnabled(true);
    } else if (mode == 0x02) {
      Serial.println("ADS debug info via hex");
      sendUsbAdsDebug();
    }
  }

  bool supportsAdsReset() {
    return true;
  }

  void adsReset(uint8_t mode) {
    handleAdsReset(mode);
  }
};

class MyUsbCallbacks {
public:
  // Function pointer members
  void (*setStableOutputThreshold)(float);
  void (*setTrackingThreshold)(float);
  void (*setTrackingUpdateInterval)(float);
  void (*buttonSquare_Pressed)();
  void (*buttonCircle_Pressed)();

  static const size_t USB_RX_BUFFER_SIZE = 160;
  static const unsigned long USB_RX_FRAME_TIMEOUT_MS = 30;

  uint8_t usbRxBuffer[USB_RX_BUFFER_SIZE];
  size_t usbRxLen = 0;
  unsigned long usbLastByteAt = 0;

  void consumeUsbRxBytes(size_t count) {
    if (count >= usbRxLen) {
      usbRxLen = 0;
      return;
    }
    memmove(usbRxBuffer, usbRxBuffer + count, usbRxLen - count);
    usbRxLen -= count;
  }

  void appendToUsbRxBuffer(uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) {
      return;
    }

    usbLastByteAt = millis();
    if (len > USB_RX_BUFFER_SIZE) {
      data += len - USB_RX_BUFFER_SIZE;
      len = USB_RX_BUFFER_SIZE;
      usbRxLen = 0;
      Serial.println("USB RX buffer overflow; keeping newest bytes");
    } else if (usbRxLen + len > USB_RX_BUFFER_SIZE) {
      usbRxLen = 0;
      Serial.println("USB RX buffer overflow; dropping buffered bytes");
    }

    memcpy(usbRxBuffer + usbRxLen, data, len);
    usbRxLen += len;
  }

  bool usbRxTimedOut() {
    return usbRxLen > 0 && millis() - usbLastByteAt >= USB_RX_FRAME_TIMEOUT_MS;
  }

  size_t bufferedTextLength(bool allowTimeout) {
    for (size_t i = 0; i < usbRxLen; i++) {
      if (usbRxBuffer[i] == 0x03) {
        return i;
      }
      if (usbRxBuffer[i] == '\n' || usbRxBuffer[i] == '\r') {
        return i + 1;
      }
    }
    return allowTimeout ? usbRxLen : 0;
  }

  void processUsbRxBuffer(bool allowTimeout) {
    while (usbRxLen > 0) {
      bool timedOut = allowTimeout && usbRxTimedOut();

      if (usbRxBuffer[0] == 0x03) {
        size_t frameLen = decentCommandFrameLength(usbRxBuffer, usbRxLen, timedOut);
        if (frameLen == 0) {
          if (timedOut) {
            Serial.println("Dropping incomplete USB binary frame");
            consumeUsbRxBytes(1);
            continue;
          }
          return;
        }

        onWrite(usbRxBuffer, frameLen);
        consumeUsbRxBytes(frameLen);
        continue;
      }

      size_t textLen = bufferedTextLength(timedOut);
      if (textLen == 0) {
        return;
      }

      onWrite(usbRxBuffer, textLen);
      consumeUsbRxBytes(textLen);
    }
  }

  void onStream(uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) {
      return;
    }

    appendToUsbRxBuffer(data, len);
    processUsbRxBuffer(false);
  }

  void poll() {
    processUsbRxBuffer(true);
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
    UsbDecentCommandSink sink;
    handleDecentBinaryCommand(sink, data, len);
  }
    
  


  void handleStringCommand(String inputString) {
    inputString.trim(); // Remove leading/trailing whitespace
    Serial.printf("handling %s\n", inputString.c_str());
    if (inputString.startsWith("welcome ")) {
      storagePutWelcome(inputString.substring(8));
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
      storagePutFloat(KEY_POUROVER, INPUTCOFFEEPOUROVER);
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
      storagePutFloat(KEY_BAT_CAL, f_batteryCalibrationFactor);

      Serial.print("Battery Voltage Factor set to: ");  // Output the new calibration factor to the Serial Monitor
      Serial.println(f_batteryCalibrationFactor);
    }

    if (inputString.startsWith("cv ")) {  //校准值
      float newCalibrationValue = inputString.substring(3).toFloat();
      if (isValidCalibrationValue(newCalibrationValue)) {
        f_calibration_value = newCalibrationValue;
        scale.setCalFactor(f_calibration_value);
        storagePutFloat(KEY_CAL1, f_calibration_value);
        b_calibrationInvalid = false;
        snprintf(c_calibrationStatus, sizeof(c_calibrationStatus), "%s",
                 calibrationRejectReasonText(CAL_REJECT_NONE));
      } else {
        Serial.print("Ignoring invalid calibration value: ");
        Serial.println(inputString.substring(3));
        b_calibrationInvalid = true;
        snprintf(c_calibrationStatus, sizeof(c_calibrationStatus), "%s",
                 calibrationRejectReasonText(
                   fabsf(newCalibrationValue) > CALIBRATION_VALUE_MAX_ABS
                     ? CAL_REJECT_FACTOR_TOO_LARGE
                     : CAL_REJECT_FACTOR_SIGN));
      }
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
      b_menu = false;
      i_cal_weight = 0;
      i_button_cal_status = 1;
      b_calibration = true;
      i_calibration = 0;
    }

    if (inputString.startsWith("cal1")) {  ////calibrate load cell 1
      b_menu = false;
      i_cal_weight = 0;
      i_button_cal_status = 1;
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
        Serial.print(" | ConvTime: "); Serial.print(info.conversionTimeMs, 3);
        Serial.print("ms | Valid: "); Serial.println(info.validSamples);
        Serial.print("CalFactor: "); Serial.print(f_calibration_value, 2);
        Serial.print(" | CalStatus: "); Serial.print(c_calibrationStatus);
        Serial.print(" | CalInvalid: "); Serial.println(b_calibrationInvalid ? "true" : "false");
        Serial.print("LastCal zero/load/delta: ");
        Serial.print(i_lastCalibrationZeroRaw);
        Serial.print(" / ");
        Serial.print(i_lastCalibrationLoadRaw);
        Serial.print(" / ");
        Serial.println(i_lastCalibrationRawDelta);
        Serial.print("LastCal candidate/verified/spread: ");
        Serial.print(f_lastCalibrationCandidate, 2);
        Serial.print(" / ");
        Serial.print(f_lastCalibrationVerifiedWeight, 2);
        Serial.print(" / ");
        Serial.println(i_lastCalibrationSpread);
        Serial.println("==============================");
      }
    }
    
#ifdef BUZZER
    if (inputString.startsWith("beep")) {  //蜂鸣器
      b_beep = !b_beep;
      storagePutInt(KEY_BEEP, b_beep);
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

// Rate-gating is handled centrally by the unified weight-output tick in the
// main loop; this just emits one line when called.
void sendUsbTextWeight() {
  Serial.print(millis());
  Serial.print(" Weight: ");
  Serial.println(f_displayedValue);
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
// [24-37] = reserved (14 bytes, zero-filled — stats not precomputed in new lib)
// [38]    = flags (bits: 0=dataOutOfRange, 1=signalTimeout, bit2 always 0)
// [39]    = reserved (1 byte, always 0 — tare is instant)
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
  uint16_t convTime = encodeScaledUInt16(info.conversionTimeMs, 100.0f);
  data[18] = (convTime >> 8) & 0xFF;
  data[19] = convTime & 0xFF;
  
  // SPS (2 bytes, float * 100)
  uint16_t sps = encodeScaledUInt16(info.sps, 100.0f);
  data[20] = (sps >> 8) & 0xFF;
  data[21] = sps & 0xFF;
  
  // Read index (1 byte)
  data[22] = info.readIndex & 0xFF;
  
  // Samples in use (1 byte)
  data[23] = info.samplesInUse & 0xFF;

  // Reset reason (1 byte, raw esp_reset_reason() code captured at boot).
  // Lets a spontaneous reboot mid-capture (brownout / panic / watchdog) be
  // attributed instead of looking like a clean power-on. Reuses the head of
  // the formerly stats-only window (24-37) freed up after the ADC library
  // swap; bytes 25-37 stay zero-filled and are reserved for future use.
  data[24] = g_resetReasonCode;
  memset(&data[25], 0, 13);
  
  // Flags (1 byte: bit 0=dataOutOfRange, bit 1=signalTimeout)
  // bit 2 (tareInProgress) always 0 — tare is instant
  data[38] = (info.dataOutOfRange ? 0x01 : 0x00) |
             (info.signalTimeout ? 0x02 : 0x00);
  
  // Tare times (1 byte) — always 0
  data[39] = 0;
  
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

void handleAdsReset(uint8_t mode) {
  Serial.print("ADS reset mode 0x0");
  Serial.println(mode);

  scale.powerDown();
  delay(500);
  scale.powerUp();

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

  if (!refreshScaleDatasetAfterDiscontinuity("ADS reset")) {
    Serial.println("ADS reset FAILED: dataset refresh timeout");
    sendUsbAdsResetResponse(mode, 0x02);
    return;
  }

  if (mode == 0x02) {
    if (!tareScaleWhenAdcReady("ADS reset tare")) {
      Serial.println("ADS reset FAILED: tare refresh timeout");
      sendUsbAdsResetResponse(mode, 0x02);
      return;
    }
    Serial.println("ADS reset: tare complete");
  }
  resetScaleOutputAfterAdcDiscontinuity();

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
