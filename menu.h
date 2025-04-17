/*
   -------------------------------------------------------------------------------------
   ADS1232_ADC
   Arduino library for ADS1232 24-Bit Analog-to-Digital Converter for Weight Scales
   By Sofronio Chen July2024
   Based on HX711_ADC By Olav Kallhovd sept2017
   -------------------------------------------------------------------------------------
*/

#include <Arduino.h>
#include "ADS1232_ADC.h"

ADS1232_ADC::ADS1232_ADC(uint8_t dout, uint8_t sck, uint8_t pdwn, uint8_t a0)  //constructor
{
  doutPin = dout;
  sckPin = sck;
  pdwnPin = pdwn;
  a0Pin = a0;
}

ADS1232_ADC::ADS1232_ADC(uint8_t dout, uint8_t sck, uint8_t pdwn)  //constructor
{
  doutPin = dout;
  sckPin = sck;
  pdwnPin = pdwn;
  a0Pin = -1;
}

void ADS1232_ADC::setGain(uint8_t gain)  //value should be 32, 64 or 128*
{
  if (gain < 64) GAIN = 2;        //32, channel B
  else if (gain < 128) GAIN = 3;  //64, channel A
  else GAIN = 1;                  //128, channel A
}

//set pinMode, ADS1232 gain and power up the ADS1232
void ADS1232_ADC::begin() {
  pinMode(sckPin, OUTPUT);
  pinMode(doutPin, INPUT);
  pinMode(pdwnPin, OUTPUT);
  if(a0Pin != -1)
    pinMode(a0Pin, OUTPUT); // Set A0 pin as OUTPUT

  setGain(128);
  powerUp();
}

//set pinMode, ADS1232 selected gain and power up the ADS1232
void ADS1232_ADC::begin(uint8_t gain) {
  pinMode(sckPin, OUTPUT);
  pinMode(doutPin, INPUT);
  pinMode(pdwnPin, OUTPUT);
  pinMode(a0Pin, OUTPUT); // Set A0 pin as OUTPUT

  setGain(gain);
  powerUp();
}
void ADS1232_ADC::start(unsigned long t) {
  t += 400;
  lastDoutLowTime = millis();
  while (millis() < t) {
    update();
    yield();
  }
  tare();
  tareStatus = 0;
}

/*  start(t, dotare) with selectable tare:
*	will do conversions continuously for 't' +400 milliseconds (400ms is min. settling time at 10SPS). 
*   Running this for 1-5s in setup() - before tare() seems to improve the tare accuracy. */
void ADS1232_ADC::start(unsigned long t, bool dotare) {
  t += 400;
  lastDoutLowTime = millis();
  while (millis() < t) {
    update();
    yield();
  }
  if (dotare) {
    tare();
    tareStatus = 0;
  }
}

/*  startMultiple(t): use this if you have more than one load cell and you want to do tare and stabilization simultaneously.
*	Will do conversions continuously for 't' +400 milliseconds (400ms is min. settling time at 10SPS). 
*   Running this for 1-5s in setup() - before tare() seems to improve the tare accuracy */
int ADS1232_ADC::startMultiple(unsigned long t) {
  tareTimeoutFlag = 0;
  lastDoutLowTime = millis();
  if (startStatus == 0) {
    if (isFirst) {
      startMultipleTimeStamp = millis();
      if (t < 400) {
        startMultipleWaitTime = t + 400;  //min time for ADS1232 to be stable
      } else {
        startMultipleWaitTime = t;
      }
      isFirst = 0;
    }
    if ((millis() - startMultipleTimeStamp) < startMultipleWaitTime) {
      update();  //do conversions during stabilization time
      yield();
      return 0;
    } else {  //do tare after stabilization time is up
      static unsigned long timeout = millis() + tareTimeOut;
      doTare = 1;
      update();
      if (convRslt == 2) {
        doTare = 0;
        convRslt = 0;
        startStatus = 1;
      }
      if (!tareTimeoutDisable) {
        if (millis() > timeout) {
          tareTimeoutFlag = 1;
          return 1;  // Prevent endless loop if no ADS1232 is connected
        }
      }
    }
  }
  return startStatus;
}

/*  startMultiple(t, dotare) with selectable tare: 
*	use this if you have more than one load cell and you want to (do tare and) stabilization simultaneously.
*	Will do conversions continuously for 't' +400 milliseconds (400ms is min. settling time at 10SPS). 
*   Running this for 1-5s in setup() - before tare() seems to improve the tare accuracy */
int ADS1232_ADC::startMultiple(unsigned long t, bool dotare) {
  tareTimeoutFlag = 0;
  lastDoutLowTime = millis();
  if (startStatus == 0) {
    if (isFirst) {
      startMultipleTimeStamp = millis();
      if (t < 400) {
        startMultipleWaitTime = t + 400;  //min time for ADS1232 to be stable
      } else {
        startMultipleWaitTime = t;
      }
      isFirst = 0;
    }
    if ((millis() - startMultipleTimeStamp) < startMultipleWaitTime) {
      update();  //do conversions during stabilization time
      yield();
      return 0;
    } else {  //do tare after stabilization time is up
      if (dotare) {
        static unsigned long timeout = millis() + tareTimeOut;
        doTare = 1;
        update();
        if (convRslt == 2) {
          doTare = 0;
          convRslt = 0;
          startStatus = 1;
        }
        if (!tareTimeoutDisable) {
          if (millis() > timeout) {
            tareTimeoutFlag = 1;
            return 1;  // Prevent endless loop if no ADS1232 is connected
          }
        }
      } else return 1;
    }
  }
  return startStatus;
}

//zero the scale, wait for tare to finnish (blocking)
void ADS1232_ADC::tare() {
  uint8_t rdy = 0;
  doTare = 1;
  tareTimes = 0;
  tareTimeoutFlag = 0;
  unsigned long timeout = millis() + tareTimeOut;
  while (rdy != 2) {
    rdy = update();
    if (!tareTimeoutDisable) {
      if (millis() > timeout) {
        tareTimeoutFlag = 1;
        break;  // Prevent endless loop if no ADS1232 is connected
      }
    }
    yield();
  }
}

//zero the scale, initiate the tare operation to run in the background (non-blocking)
void ADS1232_ADC::tareNoDelay() {
  doTare = 1;
  tareTimes = 0;
  tareStatus = 0;
}

//set new calibration factor, raw data is divided by this value to convert to readable data
void ADS1232_ADC::setCalFactor(float cal) {
  calFactor = cal;
  calFactorRecip = 1 / calFactor;
}

//returns 'true' if tareNoDelay() operation is complete
bool ADS1232_ADC::getTareStatus() {
  bool t = tareStatus;
  tareStatus = 0;
  return t;
}

//returns the current calibration factor
float ADS1232_ADC::getCalFactor() {
  return calFactor;
}

//call the function update() in loop or from ISR
//if conversion is ready; read out 24 bit data and add to dataset, returns 1
//if tare operation is complete, returns 2
//else returns 0
uint8_t ADS1232_ADC::update() {
  byte dout = digitalRead(doutPin);  //check if conversion is ready
  if (!dout) {
    conversion24bit();
    lastDoutLowTime = millis();
    signalTimeoutFlag = 0;
  } else {
    //if (millis() > (lastDoutLowTime + SIGNAL_TIMEOUT))
    if (millis() - lastDoutLowTime > SIGNAL_TIMEOUT) {
      signalTimeoutFlag = 1;
    }
    convRslt = 0;
  }
  return convRslt;
}

// call the function dataWaitingAsync() in loop or from ISR to check if new data is available to read
// if conversion is ready, just call updateAsync() to read out 24 bit data and add to dataset
// returns 1 if data available , else 0
bool ADS1232_ADC::dataWaitingAsync() {
  if (dataWaiting) {
    lastDoutLowTime = millis();
    return 1;
  }
  byte dout = digitalRead(doutPin);  //check if conversion is ready
  if (!dout) {
    dataWaiting = true;
    lastDoutLowTime = millis();
    signalTimeoutFlag = 0;
    return 1;
  } else {
    //if (millis() > (lastDoutLowTime + SIGNAL_TIMEOUT))
    if (millis() - lastDoutLowTime > SIGNAL_TIMEOUT) {
      signalTimeoutFlag = 1;
    }
    convRslt = 0;
  }
  return 0;
}

// if data is available call updateAsync() to convert it and add it to the dataset.
// call getData() to get latest value
bool ADS1232_ADC::updateAsync() {
  if (dataWaiting) {
    conversion24bit();
    dataWaiting = false;
    return true;
  }
  return false;
}

float ADS1232_ADC::getData()  // return fresh data from the moving average dataset
{
  long data = 0;
  lastSmoothedData = smoothedData();
  data = lastSmoothedData - tareOffset;
  float x = (float)data * calFactorRecip;
  return x;
}

long ADS1232_ADC::smoothedData() {
  long data = 0;
  long L = 0xFFFFFF;
  long H = 0x00;
  for (uint8_t r = 0; r < (samplesInUse + IGN_HIGH_SAMPLE + IGN_LOW_SAMPLE); r++) {
#if IGN_LOW_SAMPLE
    if (L > dataSampleSet[r]) L = dataSampleSet[r];  // find lowest value
#endif
#if IGN_HIGH_SAMPLE
    if (H < dataSampleSet[r]) H = dataSampleSet[r];  // find highest value
#endif
    data += dataSampleSet[r];
  }
#if IGN_LOW_SAMPLE
  data -= L;  //remove lowest value
#endif
#if IGN_HIGH_SAMPLE
  data -= H;  //remove highest value
#endif
  //return data;
  return (data >> divBit);
}

void ADS1232_ADC::conversion24bit()  //read 24 bit data, store in dataset and start the next conversion
{
  conversionTime = micros() - conversionStartTime;
  conversionStartTime = micros();
  unsigned long data = 0;
  uint8_t dout;
  convRslt = 0;
  if (SCK_DISABLE_INTERRUPTS) noInterrupts();

  for (uint8_t i = 0; i < (24 + GAIN); i++) {  //read 24 bit data + set gain and start next conversion
    digitalWrite(sckPin, 1);
    if (SCK_DELAY) delayMicroseconds(1);  // could be required for faster mcu's, set value in config.h
    digitalWrite(sckPin, 0);
    if (i < (24)) {
      dout = digitalRead(doutPin);
      data = (data << 1) | dout;
    } else {
      if (SCK_DELAY) delayMicroseconds(1);  // could be required for faster mcu's, set value in config.h
    }
  }
  if (SCK_DISABLE_INTERRUPTS) interrupts();

  /*
	The ADS1232 output range is min. 0x800000 and max. 0x7FFFFF (the value rolls over).
	In order to convert the range to min. 0x000000 and max. 0xFFFFFF,
	the 24th bit must be changed from 0 to 1 or from 1 to 0.
	*/
  data = data ^ 0x800000;  // flip the 24th bit

  /* ADS1232 Bit 23 is acutally the sign bit. Shift by 8 to get it to the
     * right position (31), divide by 256 to restore the correct value.
     */
  //data = (data << 8) / 256;

  if (data > 0xFFFFFF) {
    dataOutOfRange = 1;
    //Serial.println("dataOutOfRange");
  }
  if (reverseVal) {
    data = 0xFFFFFF - data;
  }
  if (readIndex == samplesInUse + IGN_HIGH_SAMPLE + IGN_LOW_SAMPLE - 1) {
    readIndex = 0;
  } else {
    readIndex++;
  }
  if (data > 0) {
    convRslt++;
    dataSampleSet[readIndex] = (long)data;
    if (doTare) {
      if (tareTimes < DATA_SET) {
        tareTimes++;
      } else {
        tareOffset = smoothedData();
        tareTimes = 0;
        doTare = 0;
        tareStatus = 1;
        convRslt++;
      }
    }
  }
}

//power down the ADS1232
void ADS1232_ADC::powerDown() {
  digitalWrite(pdwnPin, LOW);
  if (SCK_DELAY) delayMicroseconds(1);
  digitalWrite(sckPin, HIGH);
  if (SCK_DELAY) delayMicroseconds(1);
}

//power up the ADS1232
void ADS1232_ADC::powerUp() {
  digitalWrite(pdwnPin, HIGH);
  if (SCK_DELAY) delayMicroseconds(1);
  digitalWrite(sckPin, LOW);
  if (SCK_DELAY) delayMicroseconds(1);
}

//get the tare offset (raw data value output without the scale "calFactor")
long ADS1232_ADC::getTareOffset() {
  return tareOffset;
}

//set new tare offset (raw data value input without the scale "calFactor")
void ADS1232_ADC::setTareOffset(long newoffset) {
  tareOffset = newoffset;
}

//for testing and debugging:
//returns current value of dataset readIndex
int ADS1232_ADC::getReadIndex() {
  return readIndex;
}

//for testing and debugging:
//returns latest conversion time in millis
float ADS1232_ADC::getConversionTime() {
  return conversionTime / 1000.0;
}

//for testing and debugging:
//returns the ADS1232 conversions ea seconds based on the latest conversion time.
//The ADS1232 can be set to 10SPS or 80SPS. For general use the recommended setting is 10SPS.
float ADS1232_ADC::getSPS() {
  float sps = 1000000.0 / conversionTime;
  return sps;
}

//for testing and debugging:
//returns the tare timeout flag from the last tare operation.
//0 = no timeout, 1 = timeout
bool ADS1232_ADC::getTareTimeoutFlag() {
  return tareTimeoutFlag;
}

void ADS1232_ADC::disableTareTimeout() {
  tareTimeoutDisable = 1;
}

long ADS1232_ADC::getSettlingTime() {
  long st = getConversionTime() * DATA_SET;
  return st;
}

//overide the number of samples in use
//value is rounded down to the nearest valid value
void ADS1232_ADC::setSamplesInUse(int samples) {
  int old_value = samplesInUse;

  if (samples <= SAMPLES) {
    if (samples == 0)  //reset to the original value
    {
      divBit = divBitCompiled;
    } else {
      samples >>= 1;
      for (divBit = 0; samples != 0; samples >>= 1, divBit++)
        ;
    }
    samplesInUse = 1 << divBit;

    //replace the value of all samples in use with the last conversion value
    if (samplesInUse != old_value) {
      for (uint8_t r = 0; r < samplesInUse + IGN_HIGH_SAMPLE + IGN_LOW_SAMPLE; r++) {
        dataSampleSet[r] = lastSmoothedData;
      }
      readIndex = 0;
    }
  }
}

//returns the current number of samples in use.
int ADS1232_ADC::getSamplesInUse() {
  return samplesInUse;
}

//resets index for dataset
void ADS1232_ADC::resetSamplesIndex() {
  readIndex = 0;
}

//Fill the whole dataset up with new conversions, i.e. after a reset/restart (this function is blocking once started)
bool ADS1232_ADC::refreshDataSet() {
  int s = getSamplesInUse() + IGN_HIGH_SAMPLE + IGN_LOW_SAMPLE;  // get number of samples in dataset
  resetSamplesIndex();
  while (s > 0) {
    update();
    yield();
    if (digitalRead(doutPin) == LOW) {  // ADS1232 dout pin is pulled low when a new conversion is ready
      getData();                        // add data to the set and start next conversion
      s--;
    }
  }
  return true;
}

//returns 'true' when the whole dataset has been filled up with conversions, i.e. after a reset/restart.
bool ADS1232_ADC::getDataSetStatus() {
  bool i = false;
  if (readIndex == samplesInUse + IGN_HIGH_SAMPLE + IGN_LOW_SAMPLE - 1) {
    i = true;
  }
  return i;
}

//returns and sets a new calibration value (calFactor) based on a known mass input
float ADS1232_ADC::getNewCalibration(float known_mass) {
  float readValue = getData();
  float exist_calFactor = getCalFactor();
  float new_calFactor;
  new_calFactor = (readValue * exist_calFactor) / known_mass;
  setCalFactor(new_calFactor);
  return new_calFactor;
}

//returns 'true' if it takes longer time then 'SIGNAL_TIMEOUT' for the dout pin to go low after a new conversion is started
bool ADS1232_ADC::getSignalTimeoutFlag() {
  return signalTimeoutFlag;
}

//reverse the output value (flip positive/negative value)
//tare/zero-offset must be re-set after calling this.
void ADS1232_ADC::setReverseOutput() {
  reverseVal = true;
}

void ADS1232_ADC::setChannelInUse(int channel) {
  channelInUse = channel;
  if (channel == 0) {
    digitalWrite(a0Pin, LOW); // Set A0 pin LOW for Channel B
  } else if (channel == 1) {
    digitalWrite(a0Pin, HIGH); // Set A0 pin HIGH for Channel A
  } else {
    // Handle invalid channel input (optional)
    // For example, you could set a default behavior or print an error message
    Serial.println("Invalid channel selection. Please input 1 or 2.");
  }
}

//returns the current number of samples in use.
int ADS1232_ADC::getChannelInUse() {
  return channelInUse;
}
