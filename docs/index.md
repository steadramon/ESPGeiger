---
layout: page
title: Home
permalink: /
nav_order: 0
---

# ESPGeiger

ESPGeiger is a free, open-source firmware that lets you collect and export radiation data using a ESP8266 or ESP32 MCU. Use it with an existing Geiger counter, or build your own, for real-time monitoring with multiple output options.

## Features
- 😃 Easy to install and configure - contribute to monitoring!
- ✅ Compatible with all generic Geiger counters with Pulse output and a range of serial based counters
- 📈 Built in webserver with graphing
- 🖥️ SD1306 Display support
- 🔴 Live CPM and μSv/h readings
- 🔢 Smoothed and averaged values over 1, 5 and 15 minutes
- 🎛️ Configurable filtering/debounce and noise control
- 📟 Accurate counting via interrupt and non-blocking functions (accuracy tested up to 100k CPM), with optional hardware counter (ESP32 only - PCNT)
- ⏲️ No dead time due to waiting for 3rd party services
- 🌐 Upload and share statistics to services online and locally via MQTT and Home Assistant automatic discovery
- 💾 Save your data locally to a Fat16/32 SDCard over SPI
- 💡 Colourful and intuitive feedback using a WS2812X NeoPixel
- 🚧 Test builds for emulating pulse and serial based counters
