#include <cassert>
#include <cstring>
#include "app_prompt_state.h"

struct TestMux { bool locked = false; };
TestMux appPromptMux;
volatile AppPromptState appPromptState;
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
  uint8_t bytes[20] = {};
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

#include "app_prompt.h"

const uint8_t show[] = {1, 'G', 'i', 'n'};
const uint8_t heartbeat[] = {3, 10, 3, 255, 255, 0, 10};

void resetPrompt() {
  testNow = 4100;
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

int main() {
  testLoopInterleaving();
  testCancelInterleaving();
  testHeartbeatInterleaving();
  testWriteInterleaving();
}
