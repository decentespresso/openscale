#ifndef ENERGY_POWER_MANAGEMENT_H
#define ENERGY_POWER_MANAGEMENT_H

#include <stdint.h>

#if HDS_ENABLE_ENERGY_MENU && defined(ESP32)
#include <esp_pm.h>

class EnergyPowerManagement {
public:
  bool begin() {
    if (initialized) return configured;

    esp_pm_config_t config = {};
    config.max_freq_mhz = 240;
    config.min_freq_mhz = 80;
    config.light_sleep_enable = true;
    if (esp_pm_configure(&config) != ESP_OK) return false;

    if (createLocks()) {
      initialized = true;
      configured = true;
      return true;
    }
    destroyLocks();
    return false;
  }

  bool applyLightSleepSetting(bool enabled) {
    lightSleepEnabled = enabled;
    return updateLocks();
  }

  bool setPerformanceCritical(bool required) {
    performanceCritical = required;
    return updateLocks();
  }

  void setSerialTransportActive(bool active) {
    serialTransportActive = active;
  }

  bool service(uint32_t) {
    if (!initialized) return false;
    return setLockHeld(serialNoLightSleepLock, serialNoLightSleepHeld,
                       lightSleepEnabled && serialTransportActive);
  }

  bool ready() const {
    return configured;
  }

private:
  bool createLocks() {
    return esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "energy_cpu_max", &stockCpuMaxLock) == ESP_OK &&
           esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "energy_no_sleep", &stockNoLightSleepLock) == ESP_OK &&
           esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "energy_performance", &performanceLock) == ESP_OK &&
           esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "energy_serial", &serialNoLightSleepLock) == ESP_OK;
  }

  void destroyLocks() {
    deleteLock(stockCpuMaxLock);
    deleteLock(stockNoLightSleepLock);
    deleteLock(performanceLock);
    deleteLock(serialNoLightSleepLock);
    stockCpuMaxHeld = false;
    stockNoLightSleepHeld = false;
    performanceHeld = false;
    serialNoLightSleepHeld = false;
  }

  static void deleteLock(esp_pm_lock_handle_t &lock) {
    if (lock != nullptr) {
      esp_pm_lock_delete(lock);
      lock = nullptr;
    }
  }

  static bool setLockHeld(esp_pm_lock_handle_t lock, bool &held, bool required) {
    if (lock == nullptr || held == required) return lock != nullptr;
    const esp_err_t result = required ? esp_pm_lock_acquire(lock) : esp_pm_lock_release(lock);
    if (result != ESP_OK) return false;
    held = required;
    return true;
  }

  bool updateLocks() {
    if (!initialized) return false;
    const bool stockLocksOk =
      setLockHeld(stockCpuMaxLock, stockCpuMaxHeld, !lightSleepEnabled) &&
      setLockHeld(stockNoLightSleepLock, stockNoLightSleepHeld, !lightSleepEnabled);
    const bool performanceOk =
      setLockHeld(performanceLock, performanceHeld, performanceCritical);
    const bool serialOk =
      setLockHeld(serialNoLightSleepLock, serialNoLightSleepHeld,
                  lightSleepEnabled && serialTransportActive);
    return stockLocksOk && performanceOk && serialOk;
  }

  bool initialized = false;
  bool configured = false;
  bool lightSleepEnabled = false;
  bool performanceCritical = false;
  bool serialTransportActive = false;
  bool stockCpuMaxHeld = false;
  bool stockNoLightSleepHeld = false;
  bool performanceHeld = false;
  bool serialNoLightSleepHeld = false;
  esp_pm_lock_handle_t stockCpuMaxLock = nullptr;
  esp_pm_lock_handle_t stockNoLightSleepLock = nullptr;
  esp_pm_lock_handle_t performanceLock = nullptr;
  esp_pm_lock_handle_t serialNoLightSleepLock = nullptr;
};
#else
class EnergyPowerManagement {
public:
  bool begin() { return true; }
  bool applyLightSleepSetting(bool) { return true; }
  bool setPerformanceCritical(bool) { return true; }
  void setSerialTransportActive(bool) {}
  bool service(uint32_t) { return true; }
  bool ready() const { return true; }
};
#endif

#endif
