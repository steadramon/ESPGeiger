---
layout: page
title: Install
permalink: /install
nav_order: 1
has_children: true
---

# Quick Install Guide

Below is the quick overview of installing ESPGeiger, for more detailed information, please visit the sub pages.

## Requirements

- PC (Mac/Windows) with Chrome or Edge
- ESP8266 or ESP32 development board
  - Wemos D1 Mini is recommended
- A pulse or compatible serial based Geiger Counter
- Dupont/jumper wires

Optional:

- OLED screen
- ESPGeiger Log kit

_OR_

- ESPGeigerHW Geiger Counter kit

## Web Install

The easiest way to install is to visit the web based installed in your Chrome or Edge browser:

1. Visit <https://install.espgeiger.com> and follow the instructions to install the latest firmware.
  - Select your Geiger Counter type from the dropdown list - in most cases this is "Pulse"
  - Once installed your ESP device will create its own Wi-Fi network with the name in the format of "**ESPGeiger-XXXXXX**"
  - The status LED of the ESP will be lit
2. On your phone or computer, connect to to the new Wi-Fi network
  - Make a note of the name of the ESPGeiger (**ESPGeiger-XXXXXX**) - we will need this soon.
3. Configure ESPGeiger to connect to your home Wi-Fi network
  - Once connected the status LED of the ESP will go out
4. Browse to http://**ESPGeiger-XXXXXX**.local
  - Once connected to your home network you can now connect to it using the IP address assigned by your router or http://ESPGeiger-XXXXXX.local

Congratulations! 🎉 ESPGeiger is installed

## Hardware connection

Note: By default the RXPIN for ESPGeiger is set to `GPIO13` - this can be changed from the Web Portal.

<img src="/img/D1mini-basicwiring.png" title="D1 mini basic wiring">

- Connect the *Pulse Output* of the Geiger Counter to `GPIO13`
- Connect the `GND` of the Geiger Counter to `GND`
