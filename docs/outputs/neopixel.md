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

The colour is based on the ratio of current `CPM` to the 5-minute average `CPM5`, giving a visual indication of how the radiation level is changing.

- No signal, the Neopixel state is set to __BLUE__
- Dropping fast (ratio below 0.5), the Neopixel state is set to __PURPLE__
- Stable or dropping (ratio 0.5 to 1.15), the Neopixel state is set to __GREEN__
- Rising (ratio 1.15 to 1.5), the Neopixel state is set to __AMBER__
- Rising fast (ratio above 1.5), the Neopixel state is set to __RED__