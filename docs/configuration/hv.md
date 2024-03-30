---
layout: page
title: High Voltage (HV)
permalink: /configuration/hv
nav_order: 10
parent: Configuration
---

# ESPGeigerHW HV Configuration

__Note__: High voltage configuration is only available on the `ESPGeigerHW` builds of ESPGeiger.

The ESPGeigerHW High Voltage is based around a Pulse-Width Modulated boost converter circuit. By controlling the PWM duty cycle we produce a stable 400V DC driving voltage for the Geiger-Muller tube.

![ESPGeigerHW HV](/img/espghv.png)