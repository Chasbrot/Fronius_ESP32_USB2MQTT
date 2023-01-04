# Fronius_ESP32_USB2MQTT
Simulates a usb drive with a SD card to collect the log files from a fronius solar inverter and send it to a mqtt server.

## Parts
- ESP32S2
- Mine (without display): https://www.waveshare.com/wiki/ESP32-S2-Pico
- Some SD Card adapter breakout board
- SD Card, any size is ok. I used an old 1GB, really doesn't matter.
- A Fronius Inverter, I have a Symo model. If you have a other model, please check the file and directory names.

## Wiring
![ESP32-S2-Pico-details-9-2](https://user-images.githubusercontent.com/58513998/210630969-079ff80d-dcb0-40c0-ae93-b1a7efa729d7.jpg)
<br>
I know, i am not the greatest picasso.

## Compiling and Flashing
--DON'T FORGET TO INSERT YOUR WIFI AND MQTT DATA!--<br>
Takes forever. Just wait. You need the ESP32TinyUSB library, available in the library manager. For the board I used the ESP32S2 Dev Module. Everything default.

## Known Bugs
1. Doesn't work under windows, because the filesystem gets corrupted. As always windows is at fault.
