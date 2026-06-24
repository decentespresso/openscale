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

#endif
