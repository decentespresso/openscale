/*
   -------------------------------------------------------------------------------------
   ADS1232_ADC
   Arduino library for ADS1232 24-Bit Analog-to-Digital Converter for Weight Scales
   By Sofronio Chen July2024
   Based on HX711_ADC By Olav Kallhovd sept2017
   -------------------------------------------------------------------------------------
*/

#ifndef ADS1232_ADC_h
#define ADS1232_ADC_h

#include <Arduino.h>
#include "ADS1232_ADC_CONFIG.h"

/*
Note: ADS1232_ADC configuration values has been moved to file config.h
*/

#define DATA_SET SAMPLES + IGN_HIGH_SAMPLE + IGN_LOW_SAMPLE  // total samples in memory

#if (SAMPLES != 1) & (SAMPLES != 2) & (SAMPLES != 4) & (SAMPLES != 8) & (SAMPLES != 16) & (SAMPLES != 32) & (SAMPLES != 64) & (SAMPLES != 128)
#error "number of SAMPLES not valid!"
#endif

#if (CHANNEL != 0) & (SAMPLES != 1)
#error "CHANNEL number is not valid!"
#endif

#if (SAMPLES == 1) & ((IGN_HIGH_SAMPLE != 0) | (IGN_LOW_SAMPLE != 0))
#error "number of SAMPLES not valid!"
#endif

#if (SAMPLES == 1)
#define DIVB 0
#elif (SAMPLES == 2)
#define DIVB 1
#elif (SAMPLES == 4)
#define DIVB 2
#elif (SAMPLES == 8)
#define DIVB 3
#elif (SAMPLES == 16)
#define DIVB 4
#elif (SAMPLES == 32)
#define DIVB 5
#elif (SAMPLES == 64)
#define DIVB 6
#elif (SAMPLES == 128)
#define DIVB 7
#endif

#define SIGNAL_TIMEOUT 100

class ADS1232_ADC {

public:
  ADS1232_ADC(uint8_t dout, uint8_t sck, uint8_t pwdn, uint8_t a0);  //constructor
  ADS1232_ADC(uint8_t dout, uint8_t sck, uint8_t pwdn);  //constructor
  void setGain(uint8_t gain = 128);                 //value must be 32, 64 or 128*
  void begin();                                     //set pinMode, ADS1232 gain and power up the ADS1232
  void begin(uint8_t gain);                         //set pinMode, ADS1232 selected gain and power up the ADS1232
  void start(unsigned long t);                      //start ADS1232 and do tare
  void start(unsigned long t, bool dotare);         //start ADS1232, do tare if selected
  int startMultiple(unsigned long t);               //start and do tare, multiple ADS1232 simultaniously
  int startMultiple(unsigned long t, bool dotare);  //start and do tare if selected, multiple ADS1232 simultaniously
  void tare();                                      //zero the scale, wait for tare to finnish (blocking)
  void tareNoDelay();                               //zero the scale, initiate the tare operation to run in the background (non-blocking)
  bool getTareStatus();                             //returns 'true' if tareNoDelay() operation is complete
  void setCalFactor(float cal);                     //set new calibration factor, raw data is divided by this value to convert to readable data
  float getCalFactor();                             //returns the current calibration factor
  float getData();                                  //returns data from the moving average dataset

  int getReadIndex();                         //for testing and debugging
  float getConversionTime();                  //for testing and debugging
  float getSPS();                             //for testing and debugging
  bool getTareTimeoutFlag();                  //for testing and debugging
  void disableTareTimeout();                  //for testing and debugging
  long getSettlingTime();                     //for testing and debugging
  void powerDown();                           //power down the ADS1232
  void powerUp();                             //power up the ADS1232
  long getTareOffset();                       //get the tare offset (raw data value output without the scale "calFactor")
  void setTareOffset(long newoffset);         //set new tare offset (raw data value input without the scale "calFactor")
  uint8_t update();                           //if conversion is ready; read out 24 bit data and add to dataset
  bool dataWaitingAsync();                    //checks if data is available to read (no conversion yet)
  bool updateAsync();                         //read available data and add to dataset
  void setSamplesInUse(int samples);          //overide number of samples in use
  int getSamplesInUse();                      //returns current number of samples in use
  void resetSamplesIndex();                   //resets index for dataset
  bool refreshDataSet();                      //Fill the whole dataset up with new conversions, i.e. after a reset/restart (this function is blocking once started)
  bool getDataSetStatus();                    //returns 'true' when the whole dataset has been filled up with conversions, i.e. after a reset/restart
  float getNewCalibration(float known_mass);  //returns and sets a new calibration value (calFactor) based on a known mass input
  bool getSignalTimeoutFlag();                //returns 'true' if it takes longer time then 'SIGNAL_TIMEOUT' for the dout pin to go low after a new conversion is started
  void setReverseOutput();                    //reverse the output value
  void setChannelInUse(int channel);          //select channel from 0 or 1, channel 0 is default
  int getChannelInUse();                      //returns current channel number

protected:
  void conversion24bit();                     //if conversion is ready: returns 24 bit data and starts the next conversion
  long smoothedData();                        //returns the smoothed data value calculated from the dataset
  uint8_t sckPin;                             //ADS1232 pd_sck pin
  uint8_t doutPin;                            //ADS1232 dout pin
  uint8_t GAIN;                               //ADS1232 GAIN
  uint8_t pdwnPin;                            //ADS1232 PWDN for power down
  uint8_t a0Pin;                              //ADS1232 A0 for choose channel
  float calFactor = 1.0;                      //calibration factor as given in function setCalFactor(float cal)
  float calFactorRecip = 1.0;                 //reciprocal calibration factor (1/calFactor), the ADS1232 raw data is multiplied by this value
  volatile long dataSampleSet[DATA_SET + 1];  // dataset, make voltile if interrupt is used
  long tareOffset = 0;
  int readIndex = 0;
  unsigned long conversionStartTime = 0;
  unsigned long conversionTime = 0;
  uint8_t isFirst = 1;
  uint8_t tareTimes = 0;
  uint8_t divBit = DIVB;
  const uint8_t divBitCompiled = DIVB;
  bool doTare = 0;
  bool startStatus = 0;
  unsigned long startMultipleTimeStamp = 0;
  unsigned long startMultipleWaitTime = 0;
  uint8_t convRslt = 0;
  bool tareStatus = 0;
  unsigned int tareTimeOut = (SAMPLES + IGN_HIGH_SAMPLE + IGN_HIGH_SAMPLE) * 150;  // tare timeout time in ms, no of samples * 150ms (10SPS + 50% margin)
  bool tareTimeoutFlag = 0;
  bool tareTimeoutDisable = 0;
  int samplesInUse = SAMPLES;
  int channelInUse = 0;
  long lastSmoothedData = 0;
  bool dataOutOfRange = 0;
  unsigned long lastDoutLowTime = 0;
  bool signalTimeoutFlag = 0;
  bool reverseVal = 0;
  bool dataWaiting = 0;
};

#endif
