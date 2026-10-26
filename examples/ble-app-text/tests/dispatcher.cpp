#include <cassert>
#include <cstdint>
#include <mutex>

#ifndef HDS_ENABLE_GRINDER
#define HDS_ENABLE_GRINDER 1
#endif
#define HDS_ENABLE_PRESSENSOR 0
#define HDS_ENABLE_ENERGY_MENU 0
#define HDS_FEATURE_PULL_OTA 1
#define portENTER_CRITICAL(mux) ((void)0)
#define portEXIT_CRITICAL(mux) ((void)0)

@COMMANDS@

struct PullOtaTargetVersion {
  uint8_t major = 0, minor = 0, patch = 0;
  bool present = false;
};
bool b_ota = false, b_pullOtaRunning = false, pendingOtaDispatching = false;
bool b_softSleep = false, b_u8g2Sleep = false, b_powerOff = false;
bool restartAllowed = false, restartMarked = false;
uint32_t wsPendingMask = 0;
uint8_t pendingSamplesInUse = 1, pendingOtaTargetMajor = 0;
uint8_t pendingOtaTargetMinor = 0, pendingOtaTargetPatch = 0;
bool pendingOtaTargetPresent = false;
unsigned long pendingResetAt = 0, pendingOtaResetAt = 0, now = 123;
std::mutex otaDispatchMutex;
int resetCount = 0, otaCount = 0, timerCount = 0;
struct {
  template<class T> void println(T) {}
  template<class T> void print(T) {}
} Serial;
struct {
  int powerSave = -1;
  void setPowerSave(int value) { powerSave = value; }
  void setContrast(int) {}
} u8g2;
struct {
  void reset() { ++timerCount; }
  void start() { ++timerCount; }
  void stop() { ++timerCount; }
} stopWatch;
struct { int getSamplesInUse() { return 1; } } scale;
constexpr int PWR_CTRL = 0, ACC_PWR_CTRL = 1, LOW = 0;
unsigned long millis() { return now; }
bool setScaleSamplesInUseWhenReady(uint8_t, const char *) { return true; }
bool chargingUiActive() { return false; }
bool fuelGaugeLegacyHardware() { return false; }
void wakeScaleFromSoftSleep(const char *) {}
void digitalWrite(int, int) {}
void remoteQueuePending(uint32_t mask) { wsPendingMask |= mask; }
void markMenuRestartRequired() { restartMarked = true; }
bool integrationCanRestart() { return restartAllowed; }
void reset() { ++resetCount; }
void wifiUpdate(const PullOtaTargetVersion &) { assert(pendingOtaDispatching); ++otaCount; }
bool remoteQueueWifiUpdate(const PullOtaTargetVersion &target) { @QUEUE_OTA@ }
void remoteFinishWifiUpdateDispatch() { @FINISH_OTA@ }
void remoteRestoreDeferredPendingLocked(uint32_t deferredMask, unsigned long resetAt) { @RESTORE@ }
void processWsPendingCmds() { @DISPATCH@ }

int main() {
#if HDS_ENABLE_GRINDER
  wsPendingMask = WSP_RESET | WSP_POWER_OFF | WSP_TIMER_START | WSP_DISPLAY_OFF;
  processWsPendingCmds();
  assert(restartMarked && resetCount == 0 && wsPendingMask == 0);
  assert(b_powerOff && timerCount == 2 && u8g2.powerSave == 1);

  b_powerOff = false;
  wsPendingMask = WSP_RESET | WSP_WIFI_UPDATE | WSP_DISPLAY_ON | WSP_POWER_OFF;
  processWsPendingCmds();
  assert(otaCount == 1 && !pendingOtaDispatching && resetCount == 0);
  assert(wsPendingMask == (WSP_DISPLAY_ON | WSP_POWER_OFF));
  assert(!b_powerOff && u8g2.powerSave == 1);
  processWsPendingCmds();
  assert(b_powerOff && u8g2.powerSave == 0 && wsPendingMask == 0);
  assert(remoteQueueWifiUpdate(PullOtaTargetVersion{}));
  processWsPendingCmds();
  assert(otaCount == 2 && !pendingOtaDispatching);

  b_powerOff = false;
  pendingResetAt = now + 1;
  wsPendingMask = WSP_RESET | WSP_POWER_OFF;
  processWsPendingCmds();
  assert(b_powerOff && wsPendingMask == WSP_RESET && resetCount == 0);
  now += 2;
  processWsPendingCmds();
  assert(wsPendingMask == 0 && resetCount == 0);

  restartAllowed = true;
#endif
  wsPendingMask = WSP_RESET;
  processWsPendingCmds();
  assert(resetCount == 1);
  restartAllowed = false;
  b_ota = true;
  wsPendingMask = WSP_OTA_RESET | WSP_POWER_OFF;
  processWsPendingCmds();
  assert(resetCount == 2 && wsPendingMask == WSP_POWER_OFF);
}
