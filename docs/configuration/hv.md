---
layout: page
title: High Voltage (HV)
permalink: /configuration/hv
nav_order: 10
parent: Configuration
---

# ESPGeigerHW HV Configuration

__Note__: The dedicated `/hv` page is available on builds with both `-D ESPG_HV` and `-D ESPG_HV_ADC` set (i.e. PWM HV generator *and* a real ADC feedback divider for live voltage readout). The ESPGeiger-HW kit ships with both enabled.

Builds with only `-D ESPG_HV` (PWM generator without feedback) expose the same `freq`/`duty` controls in the regular Config page rather than on a dedicated /hv page, since there's no live readback to justify a real-time view.

The ESPGeigerHW High Voltage is based around a Pulse-Width Modulated boost converter circuit. By controlling the PWM duty cycle we produce a stable 400V DC driving voltage for the Geiger-Muller tube.

![ESPGeigerHW HV](/img/espghv.png)