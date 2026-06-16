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

## Build flag

Enabled with `-DALERT_OUT`, set in the common `[com-esp]` block so every shipped ESP build includes it. The module is disabled by default (`alert.pin = -1`); set the pin from the **Config > Alert Out** page after flashing.

## Settings (`alert` pref group)

| Setting | Type | Default | Description |
|---|---|---|---|
| GPIO | Int | `-1` | Output pin. `-1` disables the module. Reboot to apply a change. |
| Polarity | Bool | `0` (active high) | `0` drives the pin HIGH on alert, LOW otherwise. `1` inverts both. |
| Mode | Enum | `0` (steady) | `0` holds the pin at the active level for the duration of the alert. `1` toggles at 1 Hz (50% duty) while alerting. |

The alert threshold itself lives under [Counter Settings](/configuration/configuration#counter-settings) (`counter.alert_cpm`). Alert Out only reacts to the alert threshold; the warning threshold is not surfaced here.

## Pin safety

Alert Out uses the shared **PinSafety** registry, so it won't claim a GPIO already taken by another module (Pulse Out, blip LED, NeoPixel, OLED bus, geiger pulse input). On conflict the module logs the reason at boot and stays inactive.

## Combining with other outputs

Independent of [Audio Tick](/output/audiotick), [Pulse Out](/output/pulseout), and [NeoPixel](/output/neopixel). Run any combination on a single device.
