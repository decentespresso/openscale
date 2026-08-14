// Minimal I2C bus scan for the HDS board.
#include <Arduino.h>
#include <Wire.h>

#define I2C_SDA 5
#define I2C_SCL 4

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(200);
  Serial.println("I2C scan (SDA=5 SCL=4):");
  uint8_t found = 0;
  for (uint8_t a = 1; a < 128; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X\n", a);
      found++;
    }
  }
  Serial.printf("  %u device(s)\n", found);
  Serial.println("done");
}

void loop() {
  delay(1000);
}
