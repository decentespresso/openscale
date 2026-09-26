#ifndef ESPNOW_H
#define ESPNOW_H
#ifdef ESPNOW
#include <esp_now.h>
#include <WiFi.h>

static const uint8_t ESPNOW_BROADCAST_ADDRESS[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void populateEspnowCoffeeData() {
  coffeeData.b_mode = b_mode;
  coffeeData.b_extraction = b_extraction;
  coffeeData.f_flow_rate = f_flow_rate;
  coffeeData.f_displayedValue = f_displayedValue;
  coffeeData.t_extraction_begin = t_extraction_begin;
  coffeeData.t_extraction_first_drop_num = t_extraction_first_drop_num;
  coffeeData.t_extraction_last_drop = t_extraction_last_drop;
  coffeeData.t_elapsed = stopWatch.elapsed();
  coffeeData.b_running = stopWatch.isRunning();
  coffeeData.f_weight_dose = f_weight_dose;
  coffeeData.dataFlag = 3721;
  coffeeData.b_power_off = b_power_off;
}

void printEspnowSendResult(esp_err_t result) {
#ifdef DEBUG
  if (result == ESP_OK) {
    Serial.println("Broadcast message success");
  } else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
    Serial.println("ESP-NOW not Init.");
  } else if (result == ESP_ERR_ESPNOW_ARG) {
    Serial.println("Invalid Argument");
  } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
    Serial.println("Internal Error");
  } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
    Serial.println("ESP_ERR_ESPNOW_NO_MEM");
  } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
    Serial.println("Peer not found.");
  } else {
    Serial.println("Unknown error");
  }
#endif
}

void sendEspnowBroadcast() {
  populateEspnowCoffeeData();

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, ESPNOW_BROADCAST_ADDRESS, 6);
  if (!esp_now_is_peer_exist(ESPNOW_BROADCAST_ADDRESS)) {
    esp_now_add_peer(&peerInfo);
  }
  esp_err_t result = esp_now_send(ESPNOW_BROADCAST_ADDRESS, (uint8_t *)&coffeeData, sizeof(coffeeData));
  printEspnowSendResult(result);
}

void updateEspnow() {
  if (millis() - t_esp_now_refresh >= i_esp_now_interval) {
    t_esp_now_refresh = millis();
    sendEspnowBroadcast();
  }
}

void updateEspnow(int input) {
  for (int i = 0; i < input; i++) {
    sendEspnowBroadcast();
    delay(100);
  }
}

#endif
#endif
