---
layout: page
title: Home
permalink: /
nav_order: 0
---

# ESPGeiger

ESPGeiger is a free, open-source firmware that lets you collect and export radiation data using a ESP8266 or ESP32 MCU. Use it with an existing Geiger counter, for real-time monitoring with multiple output options.

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
- 💾 Save your data locally to an SDCard
- 💡 Colourful and intuitive feedback using a WS2812X NeoPixel
- 🚧 Test builds for emulating pulse and serial based counters

## About

ESPGeiger offers broad compatibility, supporting both common pulse-based Geiger counters and specific models that utilise serial communication.

By using ESPGeiger, you can transform your Geiger counter from a standalone device into a connected and informative tool. You can monitor radiation trends, share data with others, and contribute to scientific research.

- __Internet Connectivity__: ESPGeiger bridges the gap between your Geiger counter and the online world.
- __Integrated Web Interface__: Visualise the latest captured radiation data and change options easily from your Web browser.
- __Recording and Reporting__: ESPGeiger goes beyond simple data display. It can record your Geiger counter's readings over time, allowing you to track radiation trends and analyze potential fluctuations.
- __Data Sharing__:  ESPGeiger opens doors for data sharing with various platforms. Here are some popular options:
    - __MQTT__: Publish your radiation data to your MQTT message broker for integration with other smart home devices or custom applications.
    - __HomeAssistant__: Integrate your Geiger counter data into your Homeassistant dashboard for a centralized view of your smart home environment, including radiation levels.
    - __radmon.org__: Contribute to the global radiation monitoring effort by submitting your data to radmon.org, a community platform for enthusiasts and researchers.

## ESPGeiger-HW

ESPGeiger is used to power __ESPGeiger-HW__ - the fully featured IoT Geiger Counter from the creator of ESPGeiger.

ESPGeiger-HW is almost ready for release!

<img src="../img/ESPGeiger-HW-STS-5.jpg" width="75%"/>