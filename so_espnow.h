#ifndef ESPNOW_H
#define ESPNOW_H

#include <esp_now.h>
#include <WiFi.h>


void updateEspnow() {
  if (millis() > t_esp_now_refresh + i_esp_now_interval) {
    t_esp_now_refresh = millis();
    // Send data to all devices in promiscuous mode
    coffeeData.b_f_mode = b_f_mode;
    coffeeData.b_f_extraction = b_f_extraction;
    coffeeData.f_flow_rate = f_flow_rate;
    coffeeData.f_displayedValue = f_displayedValue;
    coffeeData.t_extraction_begin = t_extraction_begin;                    // Replace with your actual timestamp
    coffeeData.t_extraction_first_drop_num = t_extraction_first_drop_num;  // Replace with your actual timestamp
    coffeeData.t_extraction_last_drop = t_extraction_last_drop;            // Replace with your actual timestamp
    coffeeData.t_elapsed = stopWatch.elapsed();
    coffeeData.b_f_running = stopWatch.isRunning();
    coffeeData.f_weight_dose = f_weight_dose;
    coffeeData.dataFlag = 3721;  // Use any value to identify the type of data
    coffeeData.b_f_power_off = b_f_power_off;

    // Send data to the receiver
    uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    esp_now_peer_info_t peerInfo = {};
    memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
    if (!esp_now_is_peer_exist(broadcastAddress)) {
      esp_now_add_peer(&peerInfo);
    }
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&coffeeData, sizeof(coffeeData));
#ifdef DEBUG
    // Print results to serial monitor
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
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Handle the data sent callback if needed
}

void updateEspnow(int input) {
  for (int i = 0; i < input; i++) {
    coffeeData.b_f_mode = b_f_mode;
    coffeeData.b_f_extraction = b_f_extraction;
    coffeeData.f_flow_rate = f_flow_rate;
    coffeeData.f_displayedValue = f_displayedValue;
    coffeeData.t_extraction_begin = t_extraction_begin;                    // Replace with your actual timestamp
    coffeeData.t_extraction_first_drop_num = t_extraction_first_drop_num;  // Replace with your actual timestamp
    coffeeData.t_extraction_last_drop = t_extraction_last_drop;            // Replace with your actual timestamp
    coffeeData.t_elapsed = stopWatch.elapsed();
    coffeeData.b_f_running = stopWatch.isRunning();
    coffeeData.f_weight_dose = f_weight_dose;
    coffeeData.dataFlag = 3721;  // Use any value to identify the type of data
    coffeeData.b_f_power_off = b_f_power_off;

    // Send data to the receiver
    uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    esp_now_peer_info_t peerInfo = {};
    memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
    if (!esp_now_is_peer_exist(broadcastAddress)) {
      esp_now_add_peer(&peerInfo);
    }
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&coffeeData, sizeof(coffeeData));

#ifdef DEBUG
    // Print results to serial monitor
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
    delay(100);
  }
}


void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
// Formats MAC Address
{
  snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}

void receiveCallback(const uint8_t *macAddr, const uint8_t *data, int dataLen)
// Called when data is received
{
  // Only allow a maximum of 250 characters in the message + a null terminating byte
  char buffer[ESP_NOW_MAX_DATA_LEN + 1];
  int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
  strncpy(buffer, (const char *)data, msgLen);

  // Make sure we are null terminated
  buffer[msgLen] = 0;

  // Format the MAC address
  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);

  // Send Debug log message to the serial port
  Serial.printf("Received message from: %s - %s\n", macStr, buffer);
}


void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);
#ifdef DEBUG
  Serial.print("Last Packet Sent to: ");
  Serial.println(macStr);
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
#endif
}

#endif