---
layout: page
title: Getting started
permalink: /install
nav_order: 1
has_children: true
---

# Getting started

Six steps from a bare board to a working counter. Each one links to the full page.

## 1. What you need

- PC (Mac/Windows) with Chrome or Edge
- ESP8266 or ESP32 development board
  - [Wemos D1 Mini](https://s.click.aliexpress.com/e/_DmfPg5L) is recommended
- A pulse or compatible serial based Geiger Counter
- Dupont/jumper wires

Optional:

- OLED screen (SSD1306 0.96-inch 128x64; SH1106, SSD1309 and the 0.91-inch 128x32 SSD1306 also work)
- [ESPGeiger Log](/hardware/espgeigerlog) kit

_OR_

- [ESPGeigerHW Geiger Counter](/hardware/espgeigerhw) kit

## 2. Flash the firmware

The easiest way to install ESPGeiger is to use the web installer in your Chrome or Edge browser:

1. Visit <https://install.espgeiger.com> and follow the instructions to install the latest firmware.
  - Select your Geiger Counter type from the dropdown list - in most cases this is "Pulse"
  - An ESP32-S2 needs download mode for its first flash: hold **0**, tap **RST**, let go of **0**, then press **RST** once it is done
  - Installation takes up to two minutes
  - Once installed your ESP device will create its own Wi-Fi network with the name in the format of "**ESPGeiger-XXXXXX**"
  - The ESPGeiger-XXXXXX network appearing is the sign the install worked.
  - For [ESPGeiger-HW firmware](https://install.espgeiger.com?board=espgeigerhw) and [ESPGeiger Log firmware](https://install.espgeiger.com?board=espgeigerlog) you can follow these links directly.

## 3. Connect to Wi-Fi

If the installer offered to connect to Wi-Fi over USB and you said yes, skip to the last point.

1. On your phone or computer, connect to the new Wi-Fi network (**ESPGeiger-XXXXXX**)
  - A configuration web page should appear once connected, if not browse to <http://192.168.4.1/>
  - Make a note of the name of the ESPGeiger (**ESPGeiger-XXXXXX**) - we will need this soon.
2. Configure ESPGeiger to connect to your home Wi-Fi network
3. Browse to http://**ESPGeiger-XXXXXX**.local (replace XXXXXX with the ID of your ESPGeiger)
  - Once connected to your home network you can now connect to ESPGeiger using the IP address assigned by your router or http://ESPGeiger-XXXXXX.local

Congratulations! 🎉 ESPGeiger is installed

## 4. Wire the counter

Note: By default the RXPIN for ESPGeiger is set to `GPIO13` on ESP8266 / ESP32 / ESP32-S2 / ESP32-S3 and `GPIO4` on ESP32-C3 (its GPIO 11-17 are bonded to internal flash). Kits and boards with a fixed pin differ, see [Build Targets](/install/buildtargets). The pin can be changed from the Web Portal.

<img src="/img/D1mini-basicwiring.png" title="D1 mini basic wiring">

- Connect the *Pulse Output* of the Geiger Counter to `GPIO13` (`D7` on a Wemos D1 Mini)
- Connect the `GND` of the Geiger Counter to `GND`

<img src="/img/cajoe-wemosd1.jpg" width="50%" title="D1 mini basic wiring">

On ESP8266 and classic ESP32 boards a flashing/"blipping" status LED confirms that ESPGeiger is receiving readings from your Geiger counter. C3, S2 and S3 dev boards have no LED set: set **Status LED pin** under Config, System (advanced) first.

Specific instructions and diagrams for connecting ESPGeiger to a variety of different Geiger counters can be found in the [Hardware](/hardware) section.

## 5. Set the counter

On the device's web page, open **Config** and find the Counter settings.

- **Dose ratio** (`counter.ratio`) turns CPM into µSv/h and depends on the tube. The default, <!-- gen:const GEIGER_RATIO -->151.0<!-- /gen -->, is for the M4011 on most cheap kits. For other tubes, see the table on the [ESPGeiger-HW](/hardware/espgeigerhw) page.
- **Tube** (its own box) names your tube for Stations; **Geiger model** is a free name for the counter.
- Leave **CPM mode** and **Debounce** as they are to start. [CPM Mode](/configuration/cpm-mode) and [Counter](/configuration/counter) say when to change them.

## 6. Pick outputs

Readings show on the device's own web page straight away. To send them somewhere else, turn on what you use:

- Home Assistant or an MQTT broker: [MQTT](/output/mqtt), [Home Assistant](/output/ha)
- Public maps: [ESPGeiger Stations](/output/webapi), [Radmon.org](/output/radmon), [GMCMAP](/output/gmc), [ThingSpeak](/output/thingspeak)
- Your own server: [Webhook](/output/webhook), [JSON and Prometheus](/output/integrations)
- On the device: [OLED](/output/oled), [NeoPixel](/output/neopixel), [SD card](/output/sdcard)

Each page says what to fill in. The full list is under [Outputs](/output).
