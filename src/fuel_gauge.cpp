#include "fuel_gauge.h"
#include <Wire.h>
#include <BQ27427.h>

#define BQ_ADDR 0x55
#define CHEM_ID_1202 0x1202
#define CHARGING_CURRENT_MA 30

static BQ27427 bq;
static bool s_present = false;

static void writeControlOnly(uint16_t subcmd) {
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(0x00);
  Wire.write(subcmd & 0xFF);
  Wire.write(subcmd >> 8);
  Wire.endTransmission(true);
}

static uint16_t readControl(uint16_t subcmd) {
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(0x00);
  Wire.write(subcmd & 0xFF);
  Wire.write(subcmd >> 8);
  Wire.endTransmission(true);
  delay(2);
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(0x00);
  Wire.endTransmission(true);
  uint16_t value = 0;
  if (Wire.requestFrom(BQ_ADDR, 2) == 2) {
    value = Wire.read();
    value |= (uint16_t)Wire.read() << 8;
  }
  return value;
}

static void enforceChem1202() {
  uint16_t chemId = readControl(0x0008);
  if (chemId == CHEM_ID_1202) {
    return;
  }
  uint16_t status = readControl(0x0000);
  if (status & (1 << 13)) {
    writeControlOnly(0x8000);
    writeControlOnly(0x8000);
    delay(100);
  }
  writeControlOnly(0x0013);
  delay(1000);
  writeControlOnly(0x0031);
  delay(100);
  writeControlOnly(0x0042);
  delay(2000);
  Serial.printf("fuelGauge: chem id -> 0x%04X\n", readControl(0x0008));
}

bool fuelGaugeBegin() {
  Wire.begin(FUEL_GAUGE_I2C_SDA, FUEL_GAUGE_I2C_SCL);
  Wire.beginTransmission(BQ_ADDR);
  s_present = Wire.endTransmission() == 0;
  if (!s_present) {
    b_hasFuelGauge = false;
    return false;
  }
  s_present = bq.begin(FUEL_GAUGE_I2C_SDA, FUEL_GAUGE_I2C_SCL);
  b_hasFuelGauge = s_present;
  if (s_present) {
    Serial.printf("fuelGauge: BQ27427 @0x%02X present\n", BQ_ADDR);
    enforceChem1202();
  }
  return s_present;
}

float fuelGaugeVoltageV() {
  return bq.voltage() / 1000.0f;
}

int16_t fuelGaugeCurrentMa() {
  return bq.current();
}

int16_t fuelGaugeStandbyMa() {
  return bq.current(STBY);
}

int16_t fuelGaugeMaxLoadMa() {
  return bq.current(MAX);
}

int16_t fuelGaugePowerMw() {
  return bq.power();
}

uint16_t fuelGaugeCapacityMah() {
  return bq.capacity();
}

uint16_t fuelGaugeFullCapacityMah() {
  return bq.capacity(FULL);
}

uint8_t fuelGaugeSoHPercent() {
  return bq.soh();
}

uint8_t fuelGaugeSocPercent() {
  return (uint8_t)bq.soc();
}

float fuelGaugeTempC() {
  return bq.temperature(INTERNAL_TEMP) * 0.1f - 273.15f;
}

bool fuelGaugeCharging() {
  return fuelGaugeUsbPlugged() && !bq.dsgFlag() &&
         bq.current() > CHARGING_CURRENT_MA;
}

bool fuelGaugeFullCharged() {
  return bq.fcFlag();
}

bool fuelGaugeDischarging() {
  return bq.dsgFlag();
}

bool fuelGaugeChgAllowed() {
  return bq.chgFlag();
}

bool fuelGaugeUsbPlugged() {
  return digitalRead(FUEL_GAUGE_USB_DET) == LOW;
}

void fuelGaugeSleep() {
  (void)bq;
}
