#include <cassert>
#include <map>
#include <string>
#include "integration_state.h"

#define HDS_ENABLE_PRESSENSOR 1
#define HDS_ENABLE_GRINDER 1

enum PreferenceType { PT_INVALID, PT_U8, PT_STR };
struct StoredValue { PreferenceType type; uint8_t value; };
using Namespace = std::map<std::string, StoredValue>;
std::map<std::string, Namespace> nvs;
bool saveFails = false, corruptReadback = false;

class Preferences {
  std::string name;
public:
  bool begin(const char *value, bool = false) { name = value; return true; }
  void end() {}
  bool isKey(const char *key) const {
    const auto space = nvs.find(name);
    return space != nvs.end() && space->second.count(key) != 0;
  }
  PreferenceType getType(const char *key) const {
    return isKey(key) ? nvs.at(name).at(key).type : PT_INVALID;
  }
  uint8_t getUChar(const char *key, uint8_t fallback) const {
    if (corruptReadback && name == "hds") return fallback;
    return getType(key) == PT_U8 ? nvs.at(name).at(key).value : fallback;
  }
  size_t putUChar(const char *key, uint8_t value) {
    if (saveFails) return 0;
    nvs[name][key] = {PT_U8, value};
    return sizeof(value);
  }
};

Preferences settingsPreferences;
IntegrationMode activeIntegration = IntegrationMode::None;
IntegrationMode selectedIntegration = IntegrationMode::None;
struct Settings {
  bool enabled = false, previousWifiOnBoot = false, previousWifiOnBootSaved = false;
} pressensorSettings, grinderSettings;
constexpr const char *KEY_WIFI_BOOT = "wifi_boot";
bool storagePutBool(const char *key, bool value) {
  return settingsPreferences.putUChar(key, value) == 1;
}
enum State { GRINDER_STATE_CONNECTED, GRINDER_STATE_STOPPING };
enum Command { GRINDER_COMMAND_NONE, GRINDER_COMMAND_HELLO, GRINDER_COMMAND_ON, GRINDER_COMMAND_OFF };
struct {
  bool shutdownUnconfirmed = false, relayOn = false;
  bool menuPaused = false;
  State state = GRINDER_STATE_CONNECTED;
  Command pendingCommand = GRINDER_COMMAND_NONE;
} grinderRuntime;
bool b_ota = false, b_calibration = false;

#include "integration_settings.h"

bool b_menu = true, b_menuRestartRequired = false;
unsigned long pendingResetAt = 0;
uint32_t wsPendingMask = 0;
constexpr uint32_t WSP_RESET = 1;
int resetCount = 0, blockedCount = 0, failedSaves = 0;
std::string actionMessage, actionMessage2;
int t_actionMessageDelay = 0;
struct { void println(const char *) {} } Serial;
unsigned long millis() { return 123; }
#define portENTER_CRITICAL(mux) ((void)0)
#define portEXIT_CRITICAL(mux) ((void)0)
void remoteQueueResetAt(unsigned long at) { pendingResetAt = at; wsPendingMask |= WSP_RESET; }
void reset() { ++resetCount; }
void markMenuRestartRequired() { @MARK@ }
void leaveMenu() { @LEAVE@ }
void blockIntegrationChange() { ++blockedCount; }
void showStoredAction(const char *, bool, bool stored) { if (!stored) ++failedSaves; }
void grinderFlushSettingsIfDirty() {}
bool b_wifiOnBoot = false, b_wifiEnabled = false;
int wifiStarts = 0, grinderStarts = 0, sensorStarts = 0;
struct { bool pressureReady = false; } pressensorShot;
void wifi_init() { assert(b_wifiOnBoot); ++wifiStarts; }
void grinderRuntimeReset() { assert(!grinderSettings.enabled); }
void grinderRuntimeBegin() {
  assert(grinderSettings.enabled && !pressensorSettings.enabled);
  ++grinderStarts;
}
void grinderPauseForMenu() { grinderRuntime.menuPaused = true; }
void pressensorResetShot() { pressensorShot.pressureReady = false; }
void pressensorApplyLinkTarget() {
  assert(!grinderSettings.enabled || !pressensorSettings.enabled);
  if (pressensorSettings.enabled) ++sensorStarts;
}
void refreshMenuRows() {}
void menuActionMessageChanged() {}
void toggleIntegration(IntegrationMode mode) { @TOGGLE@ }
void dispatchReset() {
  uint32_t mask = wsPendingMask;
  const unsigned long resetAt = pendingResetAt;
  wsPendingMask = 0;
  pendingResetAt = 0;
  if (mask & WSP_RESET) { @RESET@ }
}

void testStoredSelection() {
  using Mode = IntegrationMode;
  settingsPreferences.begin("hds");
  for (const bool pressensor : {false, true}) {
    for (const bool grinder : {false, true}) {
      nvs.clear();
      nvs["pressensor"]["enabled"] = {PT_U8, uint8_t(pressensor)};
      nvs["grinder"]["enabled"] = {PT_U8, uint8_t(grinder)};
      nvs["grinder"]["target"] = {PT_U8, 20};
      assert(loadIntegrationSelection());
      const Mode expected = pressensor == grinder ? Mode::None : pressensor ? Mode::Pressensor : Mode::Grinder;
      assert(activeIntegration == expected);
      assert(selectedIntegration == expected);
      assert(pressensorSettings.enabled == (expected == Mode::Pressensor));
      assert(grinderSettings.enabled == (expected == Mode::Grinder));
      assert(nvs["grinder"]["target"].value == 20);
      nvs["pressensor"]["enabled"].value = !pressensor;
      assert(loadIntegrationSelection() && activeIntegration == expected);
    }
  }
  for (const StoredValue invalid : {StoredValue{PT_U8, 255}, StoredValue{PT_STR, 2}}) {
    nvs["hds"]["integration"] = invalid;
    assert(loadIntegrationSelection() && activeIntegration == Mode::None);
    assert(settingsPreferences.getType("integration") == PT_U8);
    assert(settingsPreferences.getUChar("integration", 255) == 0);
  }
  assert(storeIntegrationMode(Mode::Pressensor));
  assert(loadIntegrationSelection() && activeIntegration == Mode::Pressensor);
  saveFails = true;
  assert(!storeIntegrationMode(Mode::Grinder));
  assert(loadIntegrationSelection() && activeIntegration == Mode::Pressensor);
  nvs["hds"].clear();
  assert(!loadIntegrationSelection() && activeIntegration == Mode::Pressensor);
  saveFails = false;
  corruptReadback = true;
  assert(!storeIntegrationMode(Mode::Grinder));
  corruptReadback = false;
  assert(loadIntegrationSelection() && activeIntegration == Mode::Grinder);
  nvs["hds"].clear();
  grinderSettings.enabled = true;
  grinderSettings.previousWifiOnBootSaved = true;
  grinderSettings.previousWifiOnBoot = false;
  assert(loadIntegrationSelection());
  assert(settingsPreferences.getUChar(KEY_WIFI_BOOT, 255) == 0);
  nvs["hds"].clear();
  grinderSettings.enabled = true;
  saveFails = true;
  assert(!loadIntegrationSelection());
  saveFails = false;
}

void testRestartGuard() {
  assert(integrationCanRestart());
  grinderRuntime.pendingCommand = GRINDER_COMMAND_HELLO;
  assert(!integrationCanRestart());
  grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
  grinderRuntime.shutdownUnconfirmed = true;
  assert(!integrationCanRestart());
  grinderRuntime.relayOn = false;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
  assert(!integrationCanRestart());
  grinderRuntime.shutdownUnconfirmed = false;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_OFF;
  assert(!integrationCanRestart());
  grinderRuntime.pendingCommand = GRINDER_COMMAND_ON;
  assert(!integrationCanRestart());
  grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
  grinderRuntime.state = GRINDER_STATE_STOPPING;
  assert(!integrationCanRestart());
  grinderRuntime.state = GRINDER_STATE_CONNECTED;
  b_ota = true;
  assert(!integrationCanRestart());
  b_ota = false;
  b_calibration = true;
  assert(!integrationCanRestart());
  b_calibration = false;
  assert(integrationCanRestart());
}

void testImmediateMenuSelection() {
  using Mode = IntegrationMode;
  for (const Mode initial : {Mode::None, Mode::Pressensor, Mode::Grinder}) {
    assert(storeIntegrationMode(initial));
    assert(loadIntegrationSelection());
    b_menu = true;
    b_menuRestartRequired = false;
    wsPendingMask = 0;
    resetCount = 0;
    for (const Mode next : {Mode::Grinder, Mode::Pressensor, Mode::Pressensor, Mode::Grinder}) {
      const Mode expected = selectedIntegration == next ? Mode::None : next;
      toggleIntegration(next);
      assert(selectedIntegration == expected);
      assert(settingsPreferences.getUChar(KEY_ACTIVE_INTEGRATION, 255) == static_cast<uint8_t>(expected));
      assert(activeIntegration == expected);
      assert(grinderSettings.enabled == (expected == Mode::Grinder));
      assert(pressensorSettings.enabled == (expected == Mode::Pressensor));
      assert(b_menu && !b_menuRestartRequired && wsPendingMask == 0 && resetCount == 0);
      if (grinderSettings.enabled) assert(b_wifiOnBoot && grinderRuntime.menuPaused);
      if (pressensorSettings.enabled) assert(pressensorShot.pressureReady);
    }
    const Mode saved = selectedIntegration;
    const int starts = grinderStarts + sensorStarts + wifiStarts;
    saveFails = true;
    toggleIntegration(Mode::Pressensor);
    saveFails = false;
    assert(failedSaves > 0 && selectedIntegration == saved && resetCount == 0);
    assert(activeIntegration == saved && starts == grinderStarts + sensorStarts + wifiStarts);
    leaveMenu();
    assert(!b_menu && !b_menuRestartRequired && wsPendingMask == 0 && resetCount == 0);
    dispatchReset();
    assert(resetCount == 0);
    assert(loadIntegrationSelection() && activeIntegration == saved && selectedIntegration == saved);
  }
  assert(grinderStarts > 0 && sensorStarts > 0 && wifiStarts > 0);
  assert(storeIntegrationMode(Mode::Pressensor) && loadIntegrationSelection());
  b_menu = true;
  grinderRuntime.shutdownUnconfirmed = true;
  toggleIntegration(Mode::Grinder);
  assert(blockedCount > 0 && selectedIntegration == Mode::Pressensor && !b_menuRestartRequired);
  grinderRuntime.shutdownUnconfirmed = false;
}

void testDeferredUnsafeRestart() {
  b_menu = true;
  resetCount = 0;
  markMenuRestartRequired();
  grinderRuntime.shutdownUnconfirmed = true;
  leaveMenu();
  dispatchReset();
  assert(!b_menu && resetCount == 0 && b_menuRestartRequired && wsPendingMask == 0);
  grinderRuntime.shutdownUnconfirmed = false;
  dispatchReset();
  assert(resetCount == 0);
  b_menu = true;
  leaveMenu();
  dispatchReset();
  assert(resetCount == 1);
}

int main() {
  testStoredSelection();
  testRestartGuard();
  testImmediateMenuSelection();
  testDeferredUnsafeRestart();
  using Mode = IntegrationMode;
  assert(resolveIntegrationMode(false, 0, false, false, true, true) == Mode::None);
  assert(resolveIntegrationMode(false, 0, true, true, true, true) == Mode::None);
  assert(resolveIntegrationMode(false, 0, true, false, true, true) == Mode::Pressensor);
  assert(resolveIntegrationMode(false, 0, false, true, true, true) == Mode::Grinder);
  assert(resolveIntegrationMode(false, 0, true, false, false, true) == Mode::None);
  assert(resolveIntegrationMode(true, 2, true, false, true, true) == Mode::Grinder);
  assert(resolveIntegrationMode(true, 2, false, false, true, false) == Mode::None);
  assert(resolveIntegrationMode(true, 255, false, true, true, true) == Mode::None);
  assert(integrationRestartSafe(false, false, false, false));
  assert(!integrationRestartSafe(true, false, false, false));
  assert(!integrationRestartSafe(false, true, false, false));
  assert(!integrationRestartSafe(false, false, true, false));
  assert(!integrationRestartSafe(false, false, false, true));
}
