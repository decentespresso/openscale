#include <cassert>
#include <cstring>
#include "app_prompt_state.h"

struct TestMux { bool locked = false; };
TestMux appPromptMux;
volatile AppPromptState appPromptState;
volatile bool appTextEnabled = false;
constexpr const char *KEY_APP_TEXT = "app_text";
bool saveFails = false, savedOptIn = false;

bool storagePutBool(const char *key, bool value) {
  assert(std::strcmp(key, KEY_APP_TEXT) == 0);
  if (saveFails) return false;
  savedOptIn = value;
  return true;
}
bool b_ota = false;
bool b_ble_enabled = true;
unsigned long testNow = 4100;
void (*beforeLock)() = nullptr;

unsigned long millis() { return testNow; }

void enterCritical(TestMux *mux) {
  assert(!mux->locked);
  if (beforeLock) {
    const auto operation = beforeLock;
    beforeLock = nullptr;
    operation();
  }
  mux->locked = true;
}

void exitCritical(TestMux *mux) {
  assert(mux->locked);
  mux->locked = false;
}

#define portENTER_CRITICAL(mux) enterCritical(mux)
#define portEXIT_CRITICAL(mux) exitCritical(mux)

class BLECharacteristic;
class BLECharacteristicCallbacks {
public:
  virtual void onRead(BLECharacteristic *) {}
  virtual void onWrite(BLECharacteristic *) {}
};

class BLECharacteristic {
public:
  static constexpr unsigned PROPERTY_READ = 1;
  static constexpr unsigned PROPERTY_WRITE = 2;
  uint8_t bytes[APP_PROMPT_MAX_BYTES + 2] = {};
  size_t length = 0;
  size_t getLength() const { return length; }
  uint8_t *getData() { return bytes; }
  void setValue(const uint8_t *value, size_t size) {
    assert(size <= sizeof(bytes));
    std::memcpy(bytes, value, size);
    length = size;
  }
  void setCallbacks(BLECharacteristicCallbacks *) {}
};

class BLEService {
public:
  BLECharacteristic characteristic;
  BLECharacteristic *createCharacteristic(const char *, unsigned) { return &characteristic; }
};

#include "app_prompt_ble.h"

const uint8_t show[] = {1, 'G', 'i', 'n'};
const uint8_t heartbeat[] = {3, 10, 3, 255, 255, 0, 10};

void resetPrompt() {
  testNow = 4100;
  setAppTextEnabled(true);
  portENTER_CRITICAL(&appPromptMux);
  storeAppPromptLocked(appPromptCommand(appPromptConnection(7, testNow), show, sizeof(show), testNow));
  portEXIT_CRITICAL(&appPromptMux);
}

void renewBeforeLock() {
  testNow = 4101;
  recordAppPromptHeartbeat(heartbeat, sizeof(heartbeat));
}

void testLoopInterleaving() {
  resetPrompt();
  beforeLock = renewBeforeLock;
  processAppPromptSession();
  assert(snapshotAppPrompt().active);
  assert(snapshotAppPrompt().heartbeatAt == 4101);
}

void testCancelInterleaving() {
  resetPrompt();
  beforeLock = renewBeforeLock;
  assert(cancelAppPrompt());
  const auto state = snapshotAppPrompt();
  assert(state.cancelled && !state.active && state.heartbeatAt == 4101);
}

void testHeartbeatInterleaving() {
  resetPrompt();
  beforeLock = renewBeforeLock;
  recordAppPromptHeartbeat(heartbeat, sizeof(heartbeat));
  const auto state = snapshotAppPrompt();
  assert(state.active && state.heartbeatAt == 4101);
}

void testWriteInterleaving() {
  resetPrompt();
  BLECharacteristic characteristic;
  characteristic.setValue(show, sizeof(show));
  AppPromptCallbacks callbacks;
  beforeLock = renewBeforeLock;
  static_cast<BLECharacteristicCallbacks &>(callbacks).onWrite(&characteristic);
  assert(snapshotAppPrompt().active);
  assert(std::strcmp(snapshotAppPrompt().text, "Gin") == 0);
}

void testOptIn() {
  BLECharacteristic characteristic;
  AppPromptCallbacks callbacks;
  auto &bleCallbacks = static_cast<BLECharacteristicCallbacks &>(callbacks);
  setAppTextEnabled(false);
  setAppPromptConnection(7);
  bleCallbacks.onRead(&characteristic);
  assert(characteristic.length == 4 && characteristic.bytes[0] == 1 && characteristic.bytes[3] == 0);
  assert(characteristic.bytes[1] == 128 && characteristic.bytes[2] == 10);
  characteristic.setValue(show, sizeof(show));
  bleCallbacks.onWrite(&characteristic);
  assert(!snapshotAppPrompt().active);
  setAppTextEnabled(true);
  assert(!snapshotAppPrompt().active);
  bleCallbacks.onWrite(&characteristic);
  assert(snapshotAppPrompt().active);
  bleCallbacks.onRead(&characteristic);
  assert(characteristic.bytes[3] == 1);
  setAppTextEnabled(false);
  assert(!snapshotAppPrompt().active && snapshotAppPrompt().text[0] == 0);
  setAppTextEnabled(true);
  assert(!snapshotAppPrompt().active);
  characteristic.setValue(show, sizeof(show));
  bleCallbacks.onWrite(&characteristic);
  assert(cancelAppPrompt());
  setAppTextEnabled(false);
  setAppTextEnabled(true);
  bleCallbacks.onWrite(&characteristic);
  assert(!snapshotAppPrompt().active);
  setAppPromptConnection(8);
  bleCallbacks.onWrite(&characteristic);
  assert(snapshotAppPrompt().active);
  testNow += APP_PROMPT_TIMEOUT_MS;
  processAppPromptSession();
  assert(!snapshotAppPrompt().active);
}

void testOptInSaveFailure() {
  setAppTextEnabled(false);
  saveFails = true;
  assert(!saveAppTextEnabled(true) && !appTextIsEnabled());
  saveFails = false;
  assert(saveAppTextEnabled(true) && savedOptIn);
  setAppTextEnabled(false);
  setAppTextEnabled(savedOptIn);
  assert(appTextIsEnabled() && !snapshotAppPrompt().active);
  saveFails = true;
  assert(!saveAppTextEnabled(false) && appTextIsEnabled());
  saveFails = false;
  assert(saveAppTextEnabled(false) && !savedOptIn && !appTextIsEnabled());
}

void testDisplayGeneration() {
  resetPrompt();
  BLECharacteristic characteristic;
  AppPromptCallbacks callbacks;
  auto &bleCallbacks = static_cast<BLECharacteristicCallbacks &>(callbacks);
  characteristic.setValue(show, sizeof(show));
  const auto initial = snapshotAppPrompt();
  recordAppPromptHeartbeat(heartbeat, sizeof(heartbeat));
  bleCallbacks.onWrite(&characteristic);
  processAppPromptSession();
  assert(snapshotAppPrompt().displayGeneration == initial.displayGeneration);
  const uint8_t changed[] = {1, 'R', 'u', 'm'};
  characteristic.setValue(changed, sizeof(changed));
  bleCallbacks.onWrite(&characteristic);
  const auto updated = snapshotAppPrompt();
  assert(updated.displayGeneration != initial.displayGeneration);
  characteristic.setValue(show, sizeof(show));
  bleCallbacks.onWrite(&characteristic);
  assert(snapshotAppPrompt().displayGeneration != initial.displayGeneration);
  const auto beforeClear = snapshotAppPrompt();
  const uint8_t clear[] = {0};
  characteristic.setValue(clear, sizeof(clear));
  bleCallbacks.onWrite(&characteristic);
  characteristic.setValue(show, sizeof(show));
  bleCallbacks.onWrite(&characteristic);
  assert(snapshotAppPrompt().displayGeneration != beforeClear.displayGeneration);
  const auto beforeReconnect = snapshotAppPrompt();
  setAppPromptConnection(APP_PROMPT_NO_CONNECTION);
  setAppPromptConnection(beforeReconnect.connectionHandle);
  bleCallbacks.onWrite(&characteristic);
  assert(snapshotAppPrompt().displayGeneration != beforeReconnect.displayGeneration);
  assert(std::strcmp(snapshotAppPrompt().text, beforeReconnect.text) == 0);
}

void testLongWrites() {
  resetPrompt();
  BLECharacteristic characteristic;
  AppPromptCallbacks callbacks;
  auto &bleCallbacks = static_cast<BLECharacteristicCallbacks &>(callbacks);
  uint8_t maximum[APP_PROMPT_MAX_BYTES + 1] = {1};
  std::memset(maximum + 1, 'M', APP_PROMPT_MAX_BYTES);
  characteristic.setValue(maximum, sizeof(maximum));
  bleCallbacks.onWrite(&characteristic);
  const auto accepted = snapshotAppPrompt();
  assert(accepted.active && std::strlen(accepted.text) == APP_PROMPT_MAX_BYTES);
  uint8_t oversized[APP_PROMPT_MAX_BYTES + 2] = {1};
  std::memset(oversized + 1, 'N', APP_PROMPT_MAX_BYTES + 1);
  characteristic.setValue(oversized, sizeof(oversized));
  bleCallbacks.onWrite(&characteristic);
  assert(std::strcmp(snapshotAppPrompt().text, accepted.text) == 0);
  assert(snapshotAppPrompt().displayGeneration == accepted.displayGeneration);
}

int main() {
  testLoopInterleaving();
  testCancelInterleaving();
  testHeartbeatInterleaving();
  testWriteInterleaving();
  testOptIn();
  testOptInSaveFailure();
  testDisplayGeneration();
  testLongWrites();
}
