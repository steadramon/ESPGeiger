---
layout: page
title: ESP Hardware
permalink: /hardware/esphardware
parent: Hardware
nav_order: 1
---

# ESPGeiger Compatible Hardware

ESPGeiger is written to be compatible with both the ESP8266 and ESP32 range of MCUs.

## ESP8266

The [Wemos D1 Mini](https://s.click.aliexpress.com/e/_DmfPg5L) (4MB) is the recommended ESP8266 MCU for ESPGeiger.

The ESP8266 ESPGeiger build is the base firmware for the official ESPGeiger-based hardware __ESPGeiger-HW__ and __ESPGeiger Log__.

## ESP32

### PCNT

The `ESP32` range of MCUs feature an in-built [hardware pulse counter](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/peripherals/pcnt.html) (`PCNT`). By default ESPGeiger uses the hardware PCNT on Pulse builds for ESP32 device. A `no_pcnt` build is also available for ESP32 which uses the same Interrupt counter mechanism as the ESP8266 builds.
