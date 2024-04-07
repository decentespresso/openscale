#ifndef GYRO_H
#define GYRO_H

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;
Adafruit_Sensor *mpu_temp;
int i_gyro_print_interval = 100;
long t_gyro_refresh = 0;

void MPU6050_init() {
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    for (int i = 0; i < 4; i++) {
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      delay(100);
    }
    while (1) {
      delay(1000);
      Serial.println("Failed to find MPU6050 chip");
    }
  }
  Serial.println("MPU6050 Found!");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  Serial.print("Accelerometer range set to: ");
  switch (mpu.getAccelerometerRange()) {
    case MPU6050_RANGE_2_G:
      Serial.println("+-2G");
      break;
    case MPU6050_RANGE_4_G:
      Serial.println("+-4G");
      break;
    case MPU6050_RANGE_8_G:
      Serial.println("+-8G");
      break;
    case MPU6050_RANGE_16_G:
      Serial.println("+-16G");
      break;
  }
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  Serial.print("Gyro range set to: ");
  switch (mpu.getGyroRange()) {
    case MPU6050_RANGE_250_DEG:
      Serial.println("+- 250 deg/s");
      break;
    case MPU6050_RANGE_500_DEG:
      Serial.println("+- 500 deg/s");
      break;
    case MPU6050_RANGE_1000_DEG:
      Serial.println("+- 1000 deg/s");
      break;
    case MPU6050_RANGE_2000_DEG:
      Serial.println("+- 2000 deg/s");
      break;
  }

  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.print("Filter bandwidth set to: ");
  switch (mpu.getFilterBandwidth()) {
    case MPU6050_BAND_260_HZ:
      Serial.println("260 Hz");
      break;
    case MPU6050_BAND_184_HZ:
      Serial.println("184 Hz");
      break;
    case MPU6050_BAND_94_HZ:
      Serial.println("94 Hz");
      break;
    case MPU6050_BAND_44_HZ:
      Serial.println("44 Hz");
      break;
    case MPU6050_BAND_21_HZ:
      Serial.println("21 Hz");
      break;
    case MPU6050_BAND_10_HZ:
      Serial.println("10 Hz");
      break;
    case MPU6050_BAND_5_HZ:
      Serial.println("5 Hz");
      break;
  }

  Serial.println("");
  delay(100);
}

double gyro_z() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  // if (millis() > t_gyro_refresh + i_gyro_print_interval) {
  //   //达到设定的gyro刷新频率后进行刷新
  //   t_gyro_refresh = millis();
  //   Serial.print("\tGyro Z: \t");
  //   Serial.println(a.acceleration.z);
  //   Serial.print("\t\tTemp: \t");
  //   Serial.println(temp.temperature);
  //   Serial.print("\t\t\tESP32 Hall: \t");
  //   Serial.println(hallRead());
  // }
  return (double)a.acceleration.z;
}

// float gyro_temp() {
//   sensors_event_t temp;
//   mpu.getEvent(&temp);
//   Serial.print("Gyro Temp: \t");
//   Serial.println(temp.temperature);
//   return a.temperature;
// }

#endif