---
layout: page
title: Neopixel
permalink: /output/neopixel
parent: Outputs
nav_order: 10
---

# Neopixel Output

The NeoPixel output for ESPGeiger gives intuitive feedback to the user by means of color and frequency.

The brightness of the NeoPixel can be configured through the Web interface configuration section. Setting to a value of `0` disables the NeoPixel output.

## Color and Frequency

The frequency of flash is calculated by the ratio of the current `CPM` value to the current `CPM5` value. As the CPM value increases the frequency of the Neopixel flash increases.

The Neopixel flashes in 4 preset colours, depending on the current environment status.

If the threshold is - 

- No readings, the Neopixel state is set to __BLUE__
- Below the __WARNING__ and __ALARM__ thresholds the Neopixel state is set to __GREEN__
- Above the __WARNING__ threshold, the Neopixel is state is set to __AMBER__
- Above the __ALERT__ threshold, the Neopixel is state is set to __RED__