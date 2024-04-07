#ifndef DECLARE_H
#define DECLARE_H
#include "so_config.h"

#include <HX711_ADC.h>
#include <AceButton.h>
#include <StopWatch.h>
// #include <BluetoothSerial.h>
// BluetoothSerial SerialBT;
HX711_ADC scale(HX711_SDA, HX711_SCL);  //HX711模数转换初始化
CoffeeData coffeeData;
StopWatch stopWatch;
using namespace ace_button;
ButtonConfig config1;
AceButton buttonSet(&config1);
AceButton buttonPlus(&config1);
AceButton buttonMinus(&config1);
AceButton buttonTare(&config1);
#ifdef FIVE_BUTTON
AceButton buttonPower(&config1);
#endif


#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
BLEServer *pServer = NULL;
BLECharacteristic *pReadCharacteristic = NULL;
BLECharacteristic *pWriteCharacteristic = NULL;
bool deviceConnected = false;

// The model byte is always 03 for Decent scales
const byte modelByte = 0x03;


namespace so_buzzer {
class Buzzer {
public:
  Buzzer(int buzzerPin, int buzzerLedPin) {
    _buzzerPin = buzzerPin;
    pinMode(_buzzerPin, OUTPUT);
    _buzzerLedPin = buzzerLedPin;
    pinMode(_buzzerLedPin, OUTPUT);
  }

  void beep(int times, int duration) {
    if (b_f_beep) {
      for (int i = 0; i < times; i++) {
        digitalWrite(_buzzerPin, HIGH);
        delay(duration);
        digitalWrite(_buzzerPin, LOW);
        delay(duration);
      }
    } else {
      for (int i = 0; i < times; i++) {
        digitalWrite(_buzzerLedPin, HIGH);
        delay(duration);
        digitalWrite(_buzzerLedPin, LOW);
        delay(duration);
      }
    }
  }

private:
  int _buzzerPin;
  int _buzzerLedPin;
};
}

using namespace so_buzzer;
Buzzer buzzer(BUZZER, BUZZER_LED);


#endif