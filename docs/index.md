---
layout: page
title: Home
permalink: /
nav_order: 0
has_children: true
---

# ESPGeiger

ESPGeiger is a free, open-source firmware that lets you collect and export radiation data using a ESP8266 or ESP32 MCU. Use it with an existing Geiger counter, for real-time monitoring with multiple output options.

## Features
- 😃 Easy to install and configure - contribute to monitoring!
- ✅ Compatible with all generic Geiger counters with Pulse output and a range of serial based counters
- 📈 Built in webserver with graphing
- 🖥️ SSD1306 Display support
- 🔴 Live CPM and μSv/h readings
- 🔢 Smoothed and averaged values over 1, 5 and 15 minutes
- 🎛️ Configurable filtering/debounce and noise control
- 📟 Accurate counting via interrupt and non-blocking functions (accuracy tested up to 100k CPM), with optional hardware counter (ESP32 only - PCNT)
- ⏲️ No dead time due to waiting for 3rd party services
- 🌐 Upload and share statistics to services online and locally via MQTT and Home Assistant automatic discovery
- 💾 Save your data locally to an SDCard
- 💡 Colourful and intuitive feedback using a WS2812X NeoPixel
- 🚧 Test builds for emulating pulse and serial based counters

## ESPGeiger-HW

ESPGeiger is used to power __ESPGeiger-HW__ - the fully featured IoT Geiger Counter from the creator of ESPGeiger.

ESPGeiger-HW is almost ready for release!

<img src="../img/ESPGeiger-HW-STS-5.jpg" width="75%"/>

## Disclaimer

{: .highlight }
> The creators of ESPGeiger make no claims about the suitability of the firmware or hardware for measuring radiation in any serious or life threatening situation. ESPGeiger is created as an educational resource for hobbyists and makers a-like.
> 
> Always avoid the high voltage elements on any Geiger Counter, many of which have exposed circuitry.
>
> Please use extreme caution and safe practice when handling and storing known radioactive sources.
>
> Stay safe!