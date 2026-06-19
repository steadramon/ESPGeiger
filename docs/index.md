---
layout: page
title: Documentation
permalink: /
nav_order: 0
has_children: true
---

# ESPGeiger

ESPGeiger is a free, open-source firmware allowing you to transform any compatible Geiger counter into a real-time, IoT-enabled radiation monitoring system using ESP8266 or ESP32 microcontrollers.

## Features

**Data Display & Analysis:**
* 🔴 **Real-time Readings:** Live CPM and μSv/h with 1, 5 and 15-minute smoothed averages.
* 📈 **Web-based Graphing:** Built-in web portal with live charts and 24-hour history.
* 🖥️ **Local Display:** SSD1306, SH1106 and SSD1309 OLEDs with runtime auto-detect.
* 🕰️ **Lifetime Tracking:** Persistent total clicks, lifetime µSv and install age, surviving reboots.

**Connectivity & Data Sharing:**
* 🌐 **Online Data Export:** Radmon.org, GMCMAP, ThingSpeak, generic Webhook and GeigerLog.
* 🏠 **Local Network:** MQTT with Home Assistant auto-discovery, and a Prometheus `/metrics` endpoint.
* 📡 **UDP / OSC Broadcast:** Multicast clicks and telemetry over OSC to any tool on the LAN (Pure Data, TouchDesigner, Node-RED, etc).
* 🛰️ **UDP-Receiver Mode:** A tubeless ESP can mirror another ESPGeiger over the air for fleet displays or aggregation.
* 💾 **Local Data Logging:** SD card export for offline analysis.
* 🔌 **Offline Mode:** Hold the onboard button at startup to disable all network functionality.

**Hardware & Compatibility:**
* ✅ **Wide Counter Compatibility:** Pulse-output and serial-based Geiger counters.
* 📟 **Accurate Counting:** Interrupt-driven and non-blocking, with ESP32 PCNT hardware counter support.
* 🎛️ **Noise Control:** Configurable filtering and debounce.
* 💡 **Visual Feedback:** WS2812X NeoPixel with rate-scaled flash and z-score trend colour.
* 🌡️ **[Environment Sensors](/hardware/env):** Auto-detected BME280 / BMP280 / AHT10/20/25/30 over I2C - temperature, humidity, pressure.

**Ease of Use & Development:**
* 😃 **Easy Setup:** Captive-portal WiFi onboarding, friendly device naming, optional web password.
* 🗂️ **Config Backup & Restore:** Portable backup blob of every editable setting, transferable between devices.
* 🚧 **Emulation Tools:** Test builds for emulating pulse and serial counters.

## ESPGeiger-HW

ESPGeiger is used to power __ESPGeiger-HW__ - the fully featured IoT Geiger Counter from the creator of ESPGeiger.

ESPGeiger-HW is almost ready for release!

<img src="../img/ESPGeiger-HW-STS-5.jpg" width="75%"/>

## Disclaimer

{: .highlight }
> The creators of ESPGeiger make no claims about the suitability of the firmware or hardware for measuring radiation in any serious or life threatening situation. ESPGeiger is created as an educational resource for hobbyists and makers a-like.
> 
**Safety First:**
> Always avoid the high voltage elements on any Geiger Counter, many of which have exposed circuitry.
>
> Please use extreme caution and safe practice when handling and storing known radioactive sources.
>
> Stay safe!
