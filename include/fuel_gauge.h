#ifndef FUEL_GAUGE_H
#define FUEL_GAUGE_H
#include <Arduino.h>
#include "fuel_gauge_engine.h"

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
enum class FuelGaugeQuiesceStatus { Running, Pending, Complete, Failed };

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
  FuelGaugeReading pendingReading;
  FuelGaugeEngine engine;
  unsigned long lastPoll = 0;
  unsigned long lastRecovery = 0;
  unsigned long lastProbe = 0;
  uint16_t desiredCapacity = 700;
  uint8_t probeAttempts = 0;
  bool probeComplete = true;
  bool probeAbsent = true;
  bool ready = false;
  bool adoptCapacity = false;
  bool capacityPersistPending = false;
  bool chargeEnabled = false;
  bool sealRequired = false;
  bool lowSocNotified = false;
  bool configurationAllowed = false;
  bool configurationPending = false;
  bool recoveryPending = false;
  bool porSeen = false;
  bool resealPending = false;
  bool quiesceRequested = false;
  bool quiesceComplete = false;
  bool quiesceFailed = false;
};

extern FuelGaugeRuntime fuelGaugeRuntime;

void fuelGaugeStartDetection(uint16_t capacity, bool adoptCapacity);
bool fuelGaugePollDetection();
bool fuelGaugeBegin(uint16_t capacity, bool adoptCapacity);
bool fuelGaugeLegacyHardware();
FuelGaugeReading fuelGaugeReading();
void fuelGaugeLoop();
void fuelGaugeSetResealPending(bool pending);
void fuelGaugeAllowConfiguration();
void fuelGaugeRequestQuiesce();
FuelGaugeQuiesceStatus fuelGaugeQuiesceStatus();
void fuelGaugeResume();
bool fuelGaugeBusy();
bool fuelGaugeDrain();
bool fuelGaugeStoreReseal(bool required);
bool fuelGaugeStoreCapacity(uint16_t capacity);
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
