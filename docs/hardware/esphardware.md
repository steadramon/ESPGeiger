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

## ESP32

### PCNT

The `ESP32` range of MCUs feature an in-built hardware pulse counter (`PCNT`). By default ESPGeiger uses the hardware PCNT on Pulse builds for ESP32 device. A `no_pcnt` build is also available for ESP32 which uses the same Interrupt counter mechanism as the ESP8266 builds.