# Decent Open Scale (aka "openscale")
Decent Open Scale
Copyright 2024 Decent Espresso International Ltd

Credits:
Invention and authorship: Chen Zhichao (aka "Sofronio")

# Introduction:
The Decent Open Scale is a full open sourced(software/hardware/cad design) BLE scale. Currently you can use it with de1app and Decent Espresso machine. But with Decent Scale API, you can use it for anything.<br />
To make it work, you need at least an ESP32 for MCU, a Loadcell for weighing, an HX711 for Loadcell ADC, a SSD1306 for OLED display, a MPU6050 for gyro function.<br />
The current PCB for HDS uses ESP32S3N16, ADS1232(instead of HX711), SH1106/SH1116(instead of SSD1306), ADS1115(for battery voltage), TP4056(battery charging), CH340K(COM to USB), REF5025(analog VDD), BMA400(instead of MPU6050, but gyro will be dropped in the future).
If you want to use it unplugged, you'll also need a 3.7v battery.<br />
If you only want to burn the firmware, please read How to upload HEX file.<br />

# Library needed:
AceButton https://github.com/bxparks/AceButton <br />
Stopwatch_RT https://github.com/RobTillaart/Stopwatch_RT <br />
HX711_ADC https://github.com/olkal/HX711_ADC <br />
u8g2 https://github.com/olikraus/u8g2 <br />
Adafruit_MPU6050 https://github.com/adafruit/Adafruit_MPU6050 <br />
Adafruit_ADS1X15 https://github.com/adafruit/Adafruit_ADS1X15 <br />
SparkFun_BMA400_Arduino_Library https://github.com/sparkfun/SparkFun_BMA400_Arduino_Library <br />

# Compile Guide:
In Arduino IDE, selete tool menu, and change:
- Board to "ESP32S3 Dev Module"<br />
- CPU Frequency to "80MHz (WiFi)"<br />
- Flash Size to "16MB (128Mb)"<br />
- Partition Scheme to "16MB Flash (3MB APP/9.9MB FATFS)"<br />

# How to upload HEX file
Web USB Flash(please use Chrome/Edge, Safari or Firefox is not supported):<br />
https://adafruit.github.io/Adafruit_WebSerial_ESPTool/ <br />
The offset values are:<br />
hds.bootloader.bin 0x0000<br />
hds.ino.partitions.bin 0x8000<br />
hds.ino.bin 0x10000<br />
This tool works great, but need to reset by pressing the button on the PCB.<br />
And as it erase the eprom, a calibration is also required.<br />

Or use OpenJumper™ Serial Assistant, link as below.(In Chinese)<br />
https://www.cnblogs.com/wind-under-the-wing/p/14686625.html <br />