ESP32-Based mechanical NMEA-183 wind sensor
=============

* a5600 is used as a wind vane angle sensor
* sealed-contact is used to count wind speed sensor ticks
* [Schematics](https://github.com/elmot/esp-windsensor-mast/blob/master/kicad/mast_top.pdf)

The wind sensor starts a WiFi access point, and then transmits wind data two ways:
* NMEA 0183 sentences via UDP port 9000. Counterparts available:
  * [ESP-32 Wind indicator](//github.com/elmot/esp-windsensor-deck)
  * [ESP8266-based UART bridge] and a charplotter *Not tested on a real chartplotter*
  * [OpenCPN](https://opencpn.org/)
  * Any other software able to receive NMEA-183 data via UDP
* An internal web page showing wind dial in real time. Any mobile or a laptop is able to display that at http://yanus.local/wind

Parameters to adjust
----
The web part of the sensor is also used to fine-tune:
* Wind angle correction
* Wind speed correction
* Wind angle averaging time
* Alarm wind angles

How to flash the main unit
----
The binaries are compiled for classic ESP32, but tested also on ESP32-S3 and ESP32-C3

There are multiple ways to flash ESP32. The easiest way is [esptool-js](https://espressif.github.io/esptool-js/). 

Take binaries from [the latest release](//github.com/elmot/esp-windsensor-mast/releases/latest). 

Flash addresses for ESP32 are
| File                    | Address |
| ----------------------- | ------- |
| bootloader.bin          | 0x1000  |
| partition-table.bin     | 0x8000  |
| ota_data_initial.bin    | 0xd000  |
| esp-windsensor-mast.bin | 0x10000 |
