#ifndef DECENT_PROTOCOL_H
#define DECENT_PROTOCOL_H

#include <Arduino.h>
#include <math.h>

static inline uint8_t decentPacketChecksum(const uint8_t *data, size_t len) {
  uint8_t xorSum = 0;
  for (size_t i = 0; i < len - 1; i++) {
    xorSum ^= data[i];
  }
  return xorSum;
}

static inline bool decentValidateChecksum(const uint8_t *data, size_t len) {
  if (len < 2) {
    return false;
  }
  return data[len - 1] == decentPacketChecksum(data, len);
}

static inline uint8_t decentPayloadChecksum(const uint8_t *data, size_t len) {
  uint8_t xorSum = 0;
  for (size_t i = 0; i < len; i++) {
    xorSum ^= data[i];
  }
  return xorSum;
}

static inline bool decentHasChecksummedFrame(const uint8_t *data, size_t len, size_t frameLen) {
  if (len < frameLen || frameLen < 2) {
    return false;
  }
  return data[frameLen - 1] == decentPayloadChecksum(data, frameLen - 1);
}

static inline size_t decentFixedFrameLength(size_t len, size_t frameLen) {
  return len >= frameLen ? frameLen : 0;
}

static inline size_t decentChecksummedOrShortFrameLength(const uint8_t *data, size_t len, size_t shortLen, size_t frameLen, bool allowShort) {
  if (decentHasChecksummedFrame(data, len, frameLen)) {
    return frameLen;
  }
  if (len < shortLen) {
    return 0;
  }
  if (allowShort || len >= frameLen) {
    return shortLen;
  }
  return 0;
}

static inline size_t decentCommandFrameLength(const uint8_t *data, size_t len, bool allowShort) {
  if (data == nullptr || len == 0) {
    return 0;
  }
  if (data[0] != 0x03) {
    return 1;
  }
  if (len < 2) {
    return 0;
  }

  switch (data[1]) {
    case 0x0F:
      return decentFixedFrameLength(len, 7);
    case 0x0A:
      if (decentHasChecksummedFrame(data, len, 7)) {
        return 7;
      }
      if (len < 3) {
        return 0;
      }
      if (data[2] == 0x01) {
        return decentChecksummedOrShortFrameLength(data, len, 6, 7, allowShort);
      }
      if (data[2] == 0x03 || data[2] == 0x04) {
        return decentChecksummedOrShortFrameLength(data, len, 4, 7, allowShort);
      }
      return decentChecksummedOrShortFrameLength(data, len, 3, 7, allowShort);
    case 0x0B:
      return decentChecksummedOrShortFrameLength(data, len, 3, 7, allowShort);
    case 0x1A:
      return decentFixedFrameLength(len, 3);
    case 0x1B:
      return decentFixedFrameLength(len, 2);
#ifdef BUZZER
    case 0x1C:
      return decentFixedFrameLength(len, 3);
#endif
    case 0x1D:
      return decentFixedFrameLength(len, 3);
    case 0x1E:
      return decentFixedFrameLength(len, 4);
    case 0x1F:
      return decentFixedFrameLength(len, 2);
    case 0x20:
      if (len < 3) {
        return 0;
      }
      return decentFixedFrameLength(len, data[2] == 0x01 ? 4 : 3);
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    case 0x21:
      return decentFixedFrameLength(len, 2);
#endif
    case 0x22:
      return decentFixedFrameLength(len, 2);
    case 0x25:
      return decentChecksummedOrShortFrameLength(data, len, 3, 4, allowShort);
    case 0x26:
      return decentChecksummedOrShortFrameLength(data, len, 3, 4, allowShort);
    default:
      return decentFixedFrameLength(len, 2);
  }
}

static inline bool decentRequireLength(const char *transport, size_t actual, size_t required, const char *commandName) {
  if (actual >= required) {
    return true;
  }
  Serial.print("Ignoring short ");
  Serial.print(transport);
  Serial.print(" packet for ");
  Serial.print(commandName);
  Serial.print(": ");
  Serial.print(actual);
  Serial.print("/");
  Serial.println(required);
  return false;
}

static inline uint8_t decentXor(const uint8_t *data, size_t len) {
  uint8_t xorValue = 0x03;
  for (size_t i = 1; i < len - 1; i++) {
    xorValue ^= data[i];
  }
  return xorValue;
}

static inline void encodeWeight(float weight, byte &byte1, byte &byte2) {
  float scaled = weight * 10.0f;
  if (!isfinite(scaled)) {
    scaled = 0.0f;
  } else if (scaled > 32767.0f) {
    scaled = 32767.0f;
  } else if (scaled < -32768.0f) {
    scaled = -32768.0f;
  }
  int16_t weightInt = (int16_t)scaled;
  uint16_t encoded = (uint16_t)weightInt;
  byte1 = (byte)((encoded >> 8) & 0xFF);
  byte2 = (byte)(encoded & 0xFF);
}

static inline void buildVoltagePacket(byte data[7]) {
  byte voltageByte1, voltageByte2;
  encodeWeight(f_batteryVoltage, voltageByte1, voltageByte2);

  data[0] = modelByte;
  data[1] = 0x22;
  data[2] = voltageByte1;
  data[3] = voltageByte2;
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = decentXor(data, 6);
}

static inline void buildHeartBeatPacket(byte data[7]) {
  data[0] = modelByte;
  data[1] = 0x0A;
  data[2] = 0x03;
  data[3] = 0xFF;
  data[4] = 0xFF;
  data[5] = 0x00;
  data[6] = 0x0A;
}

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
static inline void buildGyroPacket(byte data[7]) {
  float gyro = gyro_z();
  byte gyroByte1, gyroByte2;
  encodeWeight(gyro, gyroByte1, gyroByte2);

  data[0] = modelByte;
  data[1] = 0x21;
  data[2] = gyroByte1;
  data[3] = gyroByte2;
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = decentXor(data, 6);
}
#endif

static inline void buildWeightPacket(byte data[7]) {
  float weight = f_displayedValue;
  byte weightByte1, weightByte2;
  encodeWeight(weight, weightByte1, weightByte2);

  data[0] = modelByte;
  data[1] = 0xCE;
  data[2] = weightByte1;
  data[3] = weightByte2;
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = decentXor(data, 6);
}

static inline void buildButtonPacket(byte data[7], int buttonNumber, int buttonShortPress) {
  data[0] = modelByte;
  data[1] = 0xAA;
  data[2] = buttonNumber;
  data[3] = buttonShortPress;
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = decentXor(data, 6);
}

static inline void buildPowerOffPacket(byte data[7], int i_reason) {
  data[0] = modelByte;
  data[1] = 0x2A;
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x00;

  switch (i_reason) {
    case 0: data[2] = 0x00; break;
    case 1: data[2] = 0x10; break;
    case 2: data[2] = 0x11; break;
    case 3: data[3] = 0x20; break;
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    case 4: data[2] = 0x30; break;
#endif
    default: data[2] = 0x00; break;
  }

  data[6] = decentXor(data, 6);
}

static inline void buildLedResponsePacket(byte data[7]) {
  int major = 0, minor = 0, patch = 0;

  if (sscanf(LINE1, "FW: %d.%d.%d", &major, &minor, &patch) != 3) {
    major = 0;
    minor = 0;
    patch = 0;
  }

  byte verHigh = (byte)(((major / 10) << 4) | (major % 10));
  byte verLow = (byte)((minor << 4) | patch);

  float weight = f_displayedValue;
  byte weightByte1, weightByte2;
  encodeWeight(weight, weightByte1, weightByte2);

  bool b_is_charging = false;
#if defined(USB_DET)
  b_is_charging = (digitalRead(USB_DET) == LOW);
#else
  b_is_charging = (digitalRead(BATTERY_CHARGING) == LOW);
#endif

  byte batteryByte;
  if (b_is_charging) {
    batteryByte = 0xFF;
  } else {
    float perc = (f_batteryVoltage - showEmptyBatteryBelowVoltage) /
                 (showFullBatteryAboveVoltage - showEmptyBatteryBelowVoltage) * 100.0f;
    if (perc < 0) perc = 0;
    if (perc > 100) perc = 100;
    batteryByte = (byte)perc;
  }

  data[0] = 0x03;
  data[1] = 0x0A;
  data[2] = weightByte1;
  data[3] = weightByte2;
  data[4] = batteryByte;
  data[5] = verHigh;
  data[6] = verLow;
}

static inline void handleDecentHeartbeatFlag(uint8_t flag) {
  if (flag == 0x00) {
    b_requireHeartBeat = false;
    Serial.println("*** Heartbeat detection Off ***");
  }
  if (flag == 0x01) {
    Serial.print("*** Heartbeat detection remained ");
    if (b_requireHeartBeat)
      Serial.print("On");
    else
      Serial.print("Off");
    Serial.println(" ***");
  }
}

static inline void startDecentCalibration(uint8_t mode, const char *transport) {
  if (mode == 0x00) {
    Serial.print("Manual Calibration via ");
    Serial.println(transport);
    b_menu = false;
    i_cal_weight = 0;
    i_button_cal_status = 1;
    i_calibration = 0;
    b_calibration = true;
  } else if (mode == 0x01) {
    Serial.print("Smart Calibration via ");
    Serial.println(transport);
    b_menu = false;
    i_cal_weight = 0;
    i_button_cal_status = 1;
    i_calibration = 1;
    b_calibration = true;
  }
}

static inline void handleDecentMenuCommand(const uint8_t *data) {
  if (data[2] == 0x00) {
    if (data[3] == 0x00) {
      Serial.println("Hide menu");
      b_menu = false;
    } else if (data[3] == 0x01) {
      Serial.println("Show menu");
      b_menu = true;
    }
  } else if (data[2] == 0x01) {
    if (data[3] == 0x00) {
      Serial.println("Hide about info");
      if (b_menu)
        b_showAbout = false;
      else
        b_about = false;
    } else if (data[3] == 0x01) {
      Serial.println("Show about info");
      b_debug = false;
      b_about = true;
      b_menu = false;
    }
  } else if (data[2] == 0x02) {
    if (data[3] == 0x00) {
      Serial.println("Hide debug info");
      b_debug = false;
    } else if (data[3] == 0x01) {
      Serial.println("Show debug info");
      b_about = false;
      b_debug = true;
      b_menu = false;
    }
  }
}

static inline void handleDecentUsbWeightCommand(const uint8_t *data, size_t len) {
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
      if (multiplier > 50) multiplier = 50;

      interval = multiplier * 100;
      weightUsbNotifyInterval = interval;

      Serial.print("USB weight interval set to ");
      Serial.print(weightUsbNotifyInterval);
      Serial.println(" ms");
    }
  }
}

template <typename Sink>
static inline void handleDecentBinaryCommand(Sink &sink, uint8_t *data, size_t len) {
  if (!decentRequireLength(sink.transportName(), len, 2, "message header")) {
    return;
  }

  switch (data[1]) {
    case 0x0F:
      if (!decentRequireLength(sink.transportName(), len, 7, "tare")) {
        return;
      }
      if (decentValidateChecksum(data, len)) {
        Serial.println("Valid checksum for tare operation. Taring");
      } else {
        Serial.println("Invalid checksum for tare operation.");
        return;
      }
      sink.requestTare();
      handleDecentHeartbeatFlag(data[5]);
      break;
    case 0x0A:
      if (!decentRequireLength(sink.transportName(), len, 3, "LED/power")) {
        return;
      }
      if (data[2] == 0x00) {
        Serial.println("LED off detected. Turn off OLED.");
        sink.displayOff();
      } else if (data[2] == 0x01) {
        Serial.println("LED on detected. Turn on OLED.");
        sink.displayOn();
        if (!decentRequireLength(sink.transportName(), len, 6, "LED on")) {
          return;
        }
        handleDecentHeartbeatFlag(data[5]);
      } else if (data[2] == 0x02) {
        Serial.println("Power off detected.");
        sink.powerOff();
      } else if (data[2] == 0x03) {
        if (!decentRequireLength(sink.transportName(), len, 4, "low power")) {
          return;
        }
        if (data[3] == 0x01) {
          Serial.println("Start Low Power Mode.");
          sink.lowPowerOn();
        } else if (data[3] == 0x00) {
          Serial.println("Exit low power mode.");
          sink.lowPowerOff();
        } else if (data[3] == 0xFF) {
          if (!decentRequireLength(sink.transportName(), len, 7, "heartbeat")) {
            return;
          }
          if (data[4] == 0xFF) {
            if (data[5] == 0x00) {
              if (data[6] == 0x0A) {
                t_heartBeat = millis();
                Serial.print("*** Heartbeat at ");
                Serial.print(t_heartBeat);
                Serial.println(" ***");
              }
            }
          }
        }
      } else if (data[2] == 0x04) {
        if (!decentRequireLength(sink.transportName(), len, 4, "soft sleep")) {
          return;
        }
        if (data[3] == 0x01) {
          Serial.println("Start Soft Sleep.");
          sink.softSleepOn();
        } else if (data[3] == 0x00) {
          Serial.println("Exit Soft Sleep.");
          sink.softSleepOff();
        }
      }
      break;
    case 0x0B:
      if (!decentRequireLength(sink.transportName(), len, 3, "timer")) {
        return;
      }
      if (data[2] == 0x03) {
        Serial.println("Timer start detected.");
        sink.timerStart();
      } else if (data[2] == 0x00) {
        Serial.println("Timer stop detected.");
        sink.timerStop();
      } else if (data[2] == 0x02) {
        Serial.println("Timer zero detected.");
        sink.timerZero();
      }
      break;
    case 0x1A:
      if (!decentRequireLength(sink.transportName(), len, 3, "calibration")) {
        return;
      }
      startDecentCalibration(data[2], sink.transportName());
      break;
    case 0x1B:
      sink.wifiUpdate();
      break;
#ifdef BUZZER
    case 0x1C:
      if (!decentRequireLength(sink.transportName(), len, 3, "buzzer")) {
        return;
      }
      if (data[2] == 0x00) {
        Serial.println("Buzzer Off");
        sink.buzzerOff();
      } else if (data[2] == 0x01) {
        Serial.println("Buzzer On");
        sink.buzzerOn();
      } else if (data[2] == 0x02) {
        Serial.println("Buzzer Beep");
        sink.buzzerBeep();
      }
      break;
#endif
    case 0x1D:
      if (!decentRequireLength(sink.transportName(), len, 3, "sample settings")) {
        return;
      }
      if (data[2] == 0x00) {
        sink.setSamplesInUse(1);
      } else if (data[2] == 0x01) {
        sink.setSamplesInUse(2);
      } else if (data[2] == 0x03) {
        sink.setSamplesInUse(4);
      }
      break;
    case 0x1E:
      if (!decentRequireLength(sink.transportName(), len, 4, "menu/about/debug")) {
        return;
      }
      handleDecentMenuCommand(data);
      break;
    case 0x1F:
      sink.reset();
      break;
    case 0x20:
      if (!decentRequireLength(sink.transportName(), len, 3, "USB weight")) {
        return;
      }
      handleDecentUsbWeightCommand(data, len);
      break;
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    case 0x21:
      sink.sendGyro();
      break;
#endif
    case 0x22:
      sink.sendVoltage();
      break;
    case 0x25:
      if (!decentRequireLength(sink.transportName(), len, 3, "ADS debug")) {
        return;
      }
      sink.adsDebug(data[2]);
      break;
    case 0x26:
      if (!sink.supportsAdsReset()) {
        return;
      }
      if (!decentRequireLength(sink.transportName(), len, 3, "ADS reset")) {
        return;
      }
      if (data[2] <= 0x02) {
        sink.adsReset(data[2]);
      } else {
        Serial.print("ADS reset: unknown mode 0x");
        Serial.println(data[2], HEX);
      }
      break;
    default:
      break;
  }
}

#endif
