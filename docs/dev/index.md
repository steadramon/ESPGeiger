---
layout: page
title: Testing & Development
permalink: /dev
has_children: true
nav_order: 50
---

# Testing & Development

Pull requests are welcomed! Please [visit GitHub](https://github.com/steadramon/ESPGeiger) if you would like to contribute to the project.

## Pulse Testing

![PWM module](img/pwmmodule.jpg)

PWM modules like the above have come in handy whilst developing the counting logic.

# Test Builds

ESPGeiger offers test builds specifically designed for ESP8266 and ESP32 microcontrollers. These builds enable the device to emulate various Geiger counter functionalities:

- __Pulse Counter__: Simulates the detection of radiation events by outputting a set number of pulses on the `TXPIN`
- __GC10 Serial Counter__: Emulates a GC10 Geiger counter, transmitting simulated radiation counts through the serial interface on the `TXPIN`
- __MightOhm Serial Counter__: Mimics a MightOhm Geiger counter, providing simulated radiation data in a MightOhm-compatible format via the serial port on the `TXPIN`

Simulated values follow a Poisson distribution, mimicking the random nature of real radioactive decay.

## Emulation and Communication

In these test build modes, you can connect the `TXPIN` (transmit pin) of one ESPGeiger device to the `RXPIN` (receive pin) of another ESPGeiger device, either the same unit or a different one. This setup allows you to create a closed-loop testing environment, mimicking real-world Geiger counter communication scenarios.

_Note_: No values are submitted to public services with Test builds.