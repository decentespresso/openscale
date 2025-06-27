#ifndef DECLARE_H
#define DECLARE_H
#include "config.h"

#ifdef HX711ADC
#include <HX711_ADC.h>
HX711_ADC scale(HX711_SDA, HX711_SCL);  //HX711模数转换初始化
#endif
#ifdef ADS1232ADC
#include "ADS1232_ADC.h"
ADS1232_ADC scale(SCALE_DOUT, SCALE_SCLK, SCALE_PDWN, SCALE_A0);
#endif

#include <AceButton.h>
#include <StopWatch.h>
// #include <BluetoothSerial.h>
// BluetoothSerial SerialBT;
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
BLEServer *pServer = NULL;
BLEService *pService = NULL;
BLEAdvertising *pAdvertising = NULL;
BLECharacteristic *pReadCharacteristic = NULL;
BLECharacteristic *pWriteCharacteristic = NULL;
bool deviceConnected = false;

// The model byte is always 03 for Decent scales
const byte modelByte = 0x03;



CoffeeData coffeeData;
StopWatch stopWatch;
using namespace ace_button;
ButtonConfig config1;
AceButton buttonCircle(&config1);
AceButton buttonSquare(&config1);

#ifdef BUZZER
namespace hds_buzzer {
class Buzzer {
public:
  Buzzer(int buzzerPin) {
    _buzzerPin = buzzerPin;
    pinMode(_buzzerPin, OUTPUT);
  }

  void beep(int times, int duration) {
    _buzzerTimes = times;
    _buzzerDuration = duration;
    _buzzerTimeStamp = millis();
  }

  void off() {
    digitalWrite(BUZZER, LOW);
  }

  void check() {
    if (millis() - _buzzerTimeStamp < _buzzerDuration && b_beep) {
      digitalWrite(BUZZER, HIGH);
      isOn = true;
    } else {
      isOn = false;
      digitalWrite(BUZZER, LOW);
    }
  }
protected:
  bool isOn;

private:
  int _buzzerPin;
  int _buzzerLedPin;
  int _buzzerTimes;
  int _buzzerDuration;
  unsigned long _buzzerTimeStamp;
};
}

using namespace hds_buzzer;
Buzzer buzzer(BUZZER);
#endif

#endif