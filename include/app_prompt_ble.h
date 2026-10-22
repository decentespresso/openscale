#ifndef APP_PROMPT_BLE_H
#define APP_PROMPT_BLE_H

#include "app_prompt.h"

class AppPromptCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *characteristic) override {
    uint8_t capabilities[] = {1, APP_PROMPT_MAX_BYTES, APP_PROMPT_TIMEOUT_MS / 1000,
                              static_cast<uint8_t>(appTextIsEnabled())};
    characteristic->setValue(capabilities, sizeof(capabilities));
  }

  void onWrite(BLECharacteristic *characteristic) override {
    const size_t length = characteristic->getLength();
    if (length > APP_PROMPT_MAX_BYTES + 1) return;
    const uint8_t *bytes = characteristic->getData();
    if (bytes == nullptr) return;
    portENTER_CRITICAL(&appPromptMux);
    if (appTextEnabled || (length == 1 && bytes[0] == 0)) {
      storeAppPromptLocked(appPromptCommand(readAppPromptLocked(), bytes, length, millis()));
    }
    portEXIT_CRITICAL(&appPromptMux);
  }
};

inline void setupAppPromptCharacteristic(BLEService *service) {
  BLECharacteristic *characteristic = service->createCharacteristic(
    "8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01",
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  characteristic->setCallbacks(new AppPromptCallbacks());
}

#endif
