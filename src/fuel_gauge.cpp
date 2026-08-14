#include "fuel_gauge.h"
#include <Wire.h>
#include <BQ27427.h>

#define BQ_ADDR 0x55
#define LOW_SOC_NOTIFY_PERCENT 10
#define PROTECT_CHARGE_LIMIT 80
#define PROTECT_CHARGE_RESUME 75

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
  writeControlOnly(subcmd);
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

// The library's setChemID polls CFGUPMODE with a 50 ms timeout, which is too
// short for this gauge; do the TRM flow manually with generous timeouts and
// restore the sealed state the chip came in with.
static bool waitFlag(uint16_t mask, bool set, unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (((bq.flags() & mask) != 0) == set) {
      return true;
    }
    delay(10);
  }
  return false;
}

static bool enforceChem1202() {
  // CHEM_ID returns the chem number as hex-nibble text (0x1202 = "1202");
  // the library's chemID()/CHEM_B comparison never matches, so read raw.
  if (readControl(0x0008) == 0x1202) {
    return true;
  }
  bool wasSealed = (readControl(0x0000) >> 13) & 1;
  if (wasSealed) {
    writeControlOnly(0x8000);
    writeControlOnly(0x8000);
  }
  Serial.println("fuelGauge: switching chemistry to 1202 (4.2 V)");
  writeControlOnly(0x0013);  // SET_CFGUPDATE
  if (!waitFlag(BQ27427_FLAG_CFGUPMODE, true, 2000)) {
    Serial.println("fuelGauge: chemistry switch FAILED (cfgupdate)");
    if (wasSealed) {
      writeControlOnly(0x0020);
    }
    return false;
  }
  delay(1000);  // let IT processing stop before changing chemistry
  writeControlOnly(0x0031);  // CHEM_B
  delay(100);
  writeControlOnly(0x0042);  // SOFT_RESET
  if (!waitFlag(BQ27427_FLAG_CFGUPMODE, false, 2000)) {
    Serial.println("fuelGauge: chemistry switch FAILED (soft reset)");
    if (wasSealed) {
      writeControlOnly(0x0020);
    }
    return false;
  }
  delay(500);  // let the chem id settle after the reset
  bool ok = readControl(0x0008) == 0x1202;
  if (wasSealed) {
    writeControlOnly(0x0020);
  }
  Serial.printf("fuelGauge: chemistry switch %s\n", ok ? "OK" : "FAILED");
  return ok;
}

// Early BQ27427 batches ship with a wrong CC_GAIN sign bit (negative), which
// inverts current/power readings and the DSG flag. The value lives in RAM and
// resets to the ROM default on every POR, so re-check and fix on each boot.
static void i2cWrite(uint8_t reg, const uint8_t *data, uint8_t len) {
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(reg);
  for (uint8_t i = 0; i < len; i++) {
    Wire.write(data[i]);
  }
  Wire.endTransmission(true);
}

static uint8_t i2cRead(uint8_t reg) {
  Wire.beginTransmission(BQ_ADDR);
  Wire.write(reg);
  Wire.endTransmission(true);
  Wire.requestFrom(BQ_ADDR, 1);
  return Wire.available() ? Wire.read() : 0;
}

static void loadCcGainBlock() {
  uint8_t v = 0x69;
  i2cWrite(0x3E, &v, 1);  // subclass 105
  v = 0x00;
  i2cWrite(0x3F, &v, 1);  // block 0
}

static void fixCcGainSign() {
  loadCcGainBlock();
  uint8_t signByte = i2cRead(0x45);
  if ((signByte & 0x80) == 0) {
    return;
  }
  Serial.printf("fuelGauge: CC_GAIN sign bit set (0x%02X), fixing\n", signByte);
  writeControlOnly(0x8000);
  writeControlOnly(0x8000);
  delay(100);
  writeControlOnly(0x0013);  // SET_CFGUPDATE
  delay(1000);
  loadCcGainBlock();
  signByte &= 0x7F;
  i2cWrite(0x45, &signByte, 1);
  uint8_t sum = 0;
  for (uint8_t i = 0; i < 32; i++) {
    sum += i2cRead(0x40 + i);
  }
  uint8_t csum = 255 - sum;
  i2cWrite(0x60, &csum, 1);
  writeControlOnly(0x0042);  // SOFT_RESET
  delay(2000);
  loadCcGainBlock();
  Serial.printf("fuelGauge: CC_GAIN now 0x%02X\n", i2cRead(0x45));
}

static bool s_lowSocNotified = false;
#ifdef CHRG_CTRL
static bool s_chrgEnabled = true;
#endif

uint16_t fuelGaugeDesignCapacityMah() {
  return bq.capacity(DESIGN);
}

bool fuelGaugeSetCapacity(uint16_t mAh) {
  if (mAh < 300 || mAh > 2000) {
    return false;
  }
  Serial.printf("fuelGauge: setting design capacity to %u mAh\n", mAh);
  if (!bq.setCapacity(mAh)) {
    Serial.println("fuelGauge: set capacity FAILED");
    return false;
  }
  delay(500);
  Serial.printf("fuelGauge: design capacity now %u mAh\n",
                bq.capacity(DESIGN));
  return true;
}

// Called periodically from the main loop. Serial-notify once when the SOC
// drops below 10 %. With battery protection enabled, CHRG_CTRL gates the
// charger: cut off at 80 %, resume at 75 % (hysteresis).
void fuelGaugeLoop() {
  if (!s_present) {
    return;
  }
  uint8_t soc = fuelGaugeSocPercent();
  if (soc < LOW_SOC_NOTIFY_PERCENT && !s_lowSocNotified) {
    s_lowSocNotified = true;
    Serial.printf("fuelGauge: battery low! SOC %u%% (below %u%%)\n", soc,
                  LOW_SOC_NOTIFY_PERCENT);
  }
  if (soc >= LOW_SOC_NOTIFY_PERCENT && s_lowSocNotified) {
    s_lowSocNotified = false;
  }
#ifdef CHRG_CTRL
  if (!b_batteryProtect) {
    if (!s_chrgEnabled) {
      digitalWrite(CHRG_CTRL, HIGH);
      s_chrgEnabled = true;
    }
    return;
  }
  if (soc >= PROTECT_CHARGE_LIMIT && s_chrgEnabled) {
    digitalWrite(CHRG_CTRL, LOW);
    s_chrgEnabled = false;
    Serial.printf("fuelGauge: protect, charge cut off at %u%%\n", soc);
  } else if (soc <= PROTECT_CHARGE_RESUME && !s_chrgEnabled) {
    digitalWrite(CHRG_CTRL, HIGH);
    s_chrgEnabled = true;
    Serial.printf("fuelGauge: protect, charge resumed at %u%%\n", soc);
  }
#endif
}

void fuelGaugeProtectSet(bool enable) {
#ifdef CHRG_CTRL
  b_batteryProtect = enable;
  if (!b_batteryProtect) {
    digitalWrite(CHRG_CTRL, HIGH);
    s_chrgEnabled = true;
  }
#else
  (void)enable;
#endif
}

bool fuelGaugeBegin() {
  Wire.begin(FUEL_GAUGE_I2C_SDA, FUEL_GAUGE_I2C_SCL);
  // The gauge may still be powering up right after battery insertion; retry.
  for (int attempt = 0; attempt < 5 && !s_present; attempt++) {
    if (attempt > 0) {
      delay(200);
    }
    Wire.beginTransmission(BQ_ADDR);
    s_present = Wire.endTransmission() == 0;
  }
  if (!s_present) {
    b_hasFuelGauge = false;
    return false;
  }
  s_present = bq.begin(FUEL_GAUGE_I2C_SDA, FUEL_GAUGE_I2C_SCL);
  b_hasFuelGauge = s_present;
  if (s_present) {
    Serial.printf("fuelGauge: BQ27427 @0x%02X present\n", BQ_ADDR);
    if (!enforceChem1202()) {
      s_present = false;
      b_hasFuelGauge = false;
      return false;
    }
    fixCcGainSign();
#ifdef CHRG_CTRL
    pinMode(CHRG_CTRL, OUTPUT);
    digitalWrite(CHRG_CTRL, HIGH);
    s_chrgEnabled = true;
#endif
#ifdef GPOUT
    pinMode(GPOUT, INPUT);
#endif
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
  return digitalRead(FUEL_GAUGE_CHRG_DET) == LOW;
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
