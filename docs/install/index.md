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
  - [Wemos D1 Mini](https://s.click.aliexpress.com/e/_DmfPg5L) is recommended
- A pulse or compatible serial based Geiger Counter
- Dupont/jumper wires

Optional:

- OLED screen (SSD1305 0.96-inch 128x64)
- [ESPGeiger Log](/hardware/espgeigerlog) kit

_OR_

- [ESPGeigerHW Geiger Counter](http://docs.espgeiger.com/hardware/espgeigerhw) kit

## Firmware Install

The easiest way to install ESPGeiger is to visit the web based installed in your Chrome or Edge browser:

1. Visit <https://install.espgeiger.com> and follow the instructions to install the latest firmware.
  - Select your Geiger Counter type from the dropdown list - in most cases this is "Pulse"
  - Installation takes up to two minutes
  - Once installed your ESP device will create its own Wi-Fi network with the name in the format of "**ESPGeiger-XXXXXX**"
  - The status LED of the ESP will be lit once installation has completed.
  - For [ESPGeiger-HW firmware](https://install.espgeiger.com?board=espgeigerhw) and [ESPGeiger Log firmware](https://install.espgeiger.com?board=espgeigerlog) you can follow these links directly.

## Setup

1. On your phone or computer, connect to to the new Wi-Fi network (**ESPGeiger-XXXXXX**)
  - A configuration web page should appear once connected, if not browse to <http://192.168.4.1/>
  - Make a note of the name of the ESPGeiger (**ESPGeiger-XXXXXX**) - we will need this soon.
2. Configure ESPGeiger to connect to your home Wi-Fi network
  - Once connected the status LED of the ESP will go out
3. Browse to http://**ESPGeiger-XXXXXX**.local (replace XXXXXX with the ID of your ESPGeiger)
  - Once connected to your home network you can now connect to ESPGeiger using the IP address assigned by your router or http://ESPGeiger-XXXXXX.local

Congratulations! 🎉 ESPGeiger is installed

## Hardware connection

Note: By default the RXPIN for ESPGeiger is set to `GPIO13` - this can be changed from the Web Portal.

<img src="/img/D1mini-basicwiring.png" title="D1 mini basic wiring">

- Connect the *Pulse Output* of the Geiger Counter to `GPIO13` (`D7` on a Wemos D1 Mini)
- Connect the `GND` of the Geiger Counter to `GND`

<img src="/img/cajoe-wemosd1.jpg" width="50%" title="D1 mini basic wiring">

A flashing/"blipping" status LED on the ESP device visually confirms that ESPGeiger is receiving readings from your Geiger counter.

Specific instructions and diagrams for connecting ESPGeiger to a variety of different Geiger counters can be found here.