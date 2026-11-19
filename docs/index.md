---
layout: page
title: Home
seo:
  title: "ESPGeiger: Documentation"
permalink: /
nav_order: 0
has_children: true
---

# ESPGeiger

ESPGeiger is free, open-source firmware for ESP8266 and ESP32 boards. Wire it to a Geiger counter and it counts, shows CPM and µSv/h, keeps history, and sends readings to your network and the services you use.

## What it does

Readings
* 🔴 Live CPM and µSv/h, with 1, 5 and 15 minute averages.
* 📈 Live charts and 24 hours of history in the web page.
* 🖥️ Readings on an SSD1306, SH1106 or SSD1309 OLED, or a 128x32 SSD1306. The panel is found at boot; set the chip type on the Config page.
* 🕰️ Lifetime clicks, lifetime dose and install age, kept across restarts.

Sending data
* 🌐 Uploads to Radmon.org, GMCMAP, ThingSpeak, a webhook of your own, and GeigerLog.
* 🏠 MQTT with Home Assistant discovery, and a Prometheus `/metrics` page.
* 📡 Clicks and readings over UDP / OSC on your network, for Pure Data, TouchDesigner, Node-RED and the like.
* 🛰️ A board with no tube can mirror another ESPGeiger over the network, for an extra display.
* 💾 Logging to an SD card.
* 🔌 Offline mode: press the button at startup and release it within <!-- gen:const CORIUM_BOOT_BUTTON_RESET_S s -->5 seconds<!-- /gen --> and the network stays off. If the saved WiFi is gone, it keeps counting and opens its own hotspot.

Counters and hardware
* ✅ Works with pulse output and serial output Geiger counters.
* 📟 Counts with interrupts, or the ESP32's PCNT hardware counter.
* 🎛️ Debounce and filtering you can set, for noisy inputs.
* 💡 A WS2812 NeoPixel that flashes with the count rate and changes colour with the trend.
* 🌡️ [Environment sensors](/hardware/env): BME280, BMP280 and AHT10/20/25/30 over I2C are found at boot, for temperature, humidity and pressure.

Setup
* 😃 Wi-Fi setup from a phone (the setup portal), a name for each device, and an optional web password.
* 🗂️ Back up every setting to one file and load it on the same device or another.
* 🚧 Test builds that pretend to be a pulse or serial counter, for trying things without a tube.

## ESPGeiger-HW

__ESPGeiger-HW__ is a Geiger counter board that runs ESPGeiger, from the same maker.

<img src="../img/ESPGeiger-HW-STS-5.jpg" width="75%"/>

## Disclaimer

{: .highlight }
> The creators of ESPGeiger make no claims about the suitability of the firmware or hardware for measuring radiation in any serious or life threatening situation. ESPGeiger is created as an educational resource for hobbyists and makers alike.
> 
**Safety First:**
> Always avoid the high voltage elements on any Geiger Counter, many of which have exposed circuitry.
>
> Please use extreme caution and safe practice when handling and storing known radioactive sources.
>
> Stay safe!
