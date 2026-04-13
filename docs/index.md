---
layout: page
title: Home
permalink: /
nav_order: 0
has_children: true
---

# ESPGeiger

ESPGeiger is a free, open-source firmware allowing you to transform any compatible Geiger counter into a real-time, IoT-enabled radiation monitoring system using ESP8266 or ESP32 microcontrollers.

## Features

**Data Display & Analysis:**
* 🔴 **Real-time Readings:** Live CPM and μSv/h displays.
* 📈 **Web-based Graphing:** Integrated web server for visual data analysis.
* 🔢 **Data Smoothing:** Exponentially smoothed values over 1, 5, and 15 minutes for stable readings.
* 🖥️ **Local Display:** Support for SSD1306 OLED displays.

**Connectivity & Data Sharing:**
* 🌐 **Online Data Export:** Compatible with Radmon.org, GMCMAP (gmcmap.com), and ThingSpeak.
* 🏠 **Local Network Integration:** MQTT and Home Assistant automatic discovery for seamless LAN connectivity.
* 💾 **Local Data Logging:** Save data to an SD card for offline analysis.
* 🔌 **Offline Mode:** Disables all network functionality for standalone operation, activated by holding the onboard button during startup.

**Hardware & Compatibility:**
* ✅ **Wide Geiger Counter Compatibility:** Works with pulse output and serial-based counters.
* 📟 **Accurate Counting:** Interrupt-driven and non-blocking functions, with hardware counter support (ESP32 PCNT).
* 🎛️ **Noise Control:** Configurable filtering and debounce settings.
* 💡 **Visual Feedback:** Customizable WS2812X NeoPixel for status indication.

**Ease of Use & Development:**
* 😃 **Easy Installation & Configuration:** Get started quickly and contribute to monitoring.
* ⏲️ **Independent Operation:** No reliance on external services for real-time counting.
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