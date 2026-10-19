#ifndef FUEL_GAUGE_H
#define FUEL_GAUGE_H
#include <Arduino.h>

extern volatile bool b_hasFuelGauge;
extern volatile bool b_batteryProtect;

#ifndef FUEL_GAUGE_I2C_SDA
#define FUEL_GAUGE_I2C_SDA 5
#endif
#ifndef FUEL_GAUGE_I2C_SCL
#define FUEL_GAUGE_I2C_SCL 4
#endif
#ifndef FUEL_GAUGE_USB_DET
#define FUEL_GAUGE_USB_DET 8
#endif
#ifndef FUEL_GAUGE_CHRG_DET
#define FUEL_GAUGE_CHRG_DET 10
#endif

enum class BatteryHardware { Unknown, Legacy, Gauge };

struct FuelGaugeReading {
  uint16_t voltageMv = 0;
  uint16_t temperature = 0;
  int16_t currentMa = 0;
  int16_t powerMw = 0;
  uint16_t capacityMah = 0;
  uint16_t fullCapacityMah = 0;
  uint16_t flags = 0;
  uint16_t soc = 0;
  uint16_t soh = 0;
  uint32_t sequence = 0;
  unsigned long sampledAt = 0;
  bool valid = false;
};

struct FuelGaugeRuntime {
  BatteryHardware hardware = BatteryHardware::Unknown;
  FuelGaugeReading reading;
  unsigned long lastPoll = 0;
  unsigned long lastRecovery = 0;
  uint16_t desiredCapacity = 700;
  bool ready = false;
  bool adoptCapacity = false;
  bool capacityPersistPending = false;
  bool chargeEnabled = false;
  bool sealRequired = false;
  bool lowSocNotified = false;
};

struct FuelGaugeSleepState {
  uint32_t magic;
  uint16_t capacity;
  bool ready;
  bool chargeEnabled;
  bool sealRequired;
};

constexpr uint32_t fuelGaugeSleepMagic = 0x42415431;
extern FuelGaugeSleepState fuelGaugeSleepState;

inline bool fuelGaugeDeepSleepActive() {
  return fuelGaugeSleepState.magic == fuelGaugeSleepMagic &&
         fuelGaugeSleepState.capacity >= 300 && fuelGaugeSleepState.capacity <= 2000;
}

extern FuelGaugeRuntime fuelGaugeRuntime;

bool fuelGaugeBegin(uint16_t capacity, bool adoptCapacity);
bool fuelGaugeLegacyHardware();
FuelGaugeReading fuelGaugeReading();
void fuelGaugeLoop();
void fuelGaugePauseCharging();
void fuelGaugeProtectSet(bool enable);
float fuelGaugeVoltageV();
int16_t fuelGaugeCurrentMa();
int16_t fuelGaugePowerMw();
uint16_t fuelGaugeCapacityMah();
uint16_t fuelGaugeFullCapacityMah();
uint16_t fuelGaugeDesignCapacityMah();
bool fuelGaugeSetCapacity(uint16_t mAh);
inline bool fuelGaugeParseCapacity(const char *text, uint16_t &capacity) {
  if (!text || !*text) return false;
  unsigned int value = 0;
  for (const char *digit = text; *digit; ++digit) {
    if (*digit < '0' || *digit > '9') return false;
    value = value * 10 + (*digit - '0');
    if (value > 2000) return false;
  }
  if (value < 300) return false;
  capacity = static_cast<uint16_t>(value);
  return true;
}
uint8_t fuelGaugeSoHPercent();
uint8_t fuelGaugeSocPercent();
float fuelGaugeTempC();
bool fuelGaugeCharging();
bool fuelGaugeFullCharged();
bool fuelGaugeDischarging();
bool fuelGaugeChgAllowed();
bool fuelGaugeUsbPlugged();
void fuelGaugeSleep();

#endif
