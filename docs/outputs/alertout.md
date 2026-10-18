---
layout: page
title: Alert Out
permalink: /output/alertout
parent: Outputs
nav_order: 13
---

# Alert Out

A configurable GPIO that follows the Counter alert state. Drives a relay, sounder, external indicator, or any logic input that should fire when CPM crosses the alert threshold.

The pin idles at its inactive level and switches to the active level once the alert threshold is breached. Two output modes: steady level or a 1 Hz square wave while alerting.

Included in every shipped ESP build. Disabled by default; set the pin from **Config > Alert Out** after flashing.

## Settings

<!-- gen:prefs alert -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| GPIO | `alert.pin` | Int -1 to max GPIO | `-1` | Output pin. `-1` disables the module. Reboot to apply a change. |
| Polarity | `alert.polarity` | `0` Active high, `1` Active low | `0` Active high | Active high drives the pin HIGH on alert, LOW otherwise. Active low inverts both. |
| Mode | `alert.mode` | `0` Steady level, `1` 1 Hz pulse | `0` Steady level | Steady holds the pin at the active level for the duration of the alert. 1 Hz pulse toggles it at 1 Hz (50% duty) while alerting. |

<!-- /gen -->

Set the threshold with **Alert CPM** under [System Settings](/configuration#system-settings). Alert Out ignores the warning threshold.

## Pin safety

Alert Out will not take a GPIO already used by another feature (Pulse Out, blip LED, NeoPixel, OLED, Geiger input). On a clash it logs the reason at boot and stays off.

## Combining with other outputs

Independent of [Audio Tick](/output/audiotick), [Pulse Out](/output/pulseout) and [NeoPixel](/output/neopixel). Run any combination on a single device.
