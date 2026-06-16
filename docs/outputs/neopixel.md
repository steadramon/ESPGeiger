---
layout: page
title: Neopixel
permalink: /output/neopixel
parent: Outputs
nav_order: 10
---

# Neopixel Output

The NeoPixel output gives intuitive visual feedback by means of colour and flash pattern.

Brightness, render mode, and (for the simple blip mode) colour are all configured under **Outputs &raquo; NeoPixel** in the Web interface. Setting brightness to `0` or mode to `Off` disables the NeoPixel and unloads the module from the scheduler.

## Modes

| Mode | Triggers on | Colour |
|---|---|---|
| 0 - Off | nothing | LED dark, module unloaded |
| 1 - Blip | every click | fixed (user-chosen colour, default green) |
| 2 - Status blip | every click | green normally; yellow on warning, red on alert, blue if no clicks |
| 3 - Trend pulse | periodic, rate-derived interval | rate trend (see below) |
| 4 - Trend + status (default) | periodic, rate-derived interval | rate trend, with warning / alert / no-data overrides |

Modes 1 and 2 produce one short flash per click and work on receiver builds (UDP RX, LoRa RX) too, where the click stream is the producer's batched window.

Modes 3 and 4 produce a regular *pulse* whose rate scales with how the current 1-minute CPM compares to the 5-minute average. The pulse is a trend indicator, not a 1:1 click visualisation.

### Blip colour (mode 1)

Mode 1 uses a fixed colour you pick from a small palette:

| Value | Colour |
|---|---|
| 0 | Green (default) |
| 1 | Red |
| 2 | Blue |
| 3 | Yellow |
| 4 | Cyan |
| 5 | Magenta |
| 6 | White |
| 7 | Orange |

Brightness scales the chosen colour proportionally.

## How the trend colour and flash rate are derived (modes 3 and 4)

The pulse **rate** scales with the ratio of the current 1-minute CPM to the 5-minute average CPM (`CPM5`). Rising activity pulses faster; steady or falling activity pulses slower. The interval is clamped to between 100 ms (≈10/sec) at maximum activity and 4 s at minimum.

The pulse **colour** is derived from a *Poisson z-score* against the 5-minute baseline:

```
z = (CPM - CPM5) / sqrt(CPM5)
```

This is noise-aware: at low rates a small absolute change carries little statistical weight, while at high rates even a modest absolute change can be significant. The colour buckets:

| Condition | Colour | Meaning |
|---|---|---|
| `CPM < 0.01` and `CPM5 < 0.01` | __BLUE__ | No signal (no clicks observed) |
| `z > 3` | __RED__ | Significant rise (≥3σ above baseline), likely a real source |
| `z > 1.5` | __YELLOW__ | Rising (≥1.5σ above baseline), worth watching |
| `z < -1.5` | __PURPLE__ | Dropping (≥1.5σ below baseline) |
| _otherwise_ | __GREEN__ | Within normal variation around baseline |

In mode 4, if a [warning or alert threshold](/configuration/configuration#counter-settings) is configured and the current CPM crosses it, the NeoPixel switches to __YELLOW__ (warning) or __RED__ (alert) regardless of the z-score above. Mode 3 ignores the thresholds and shows only the trend colour.

## Why mode 3/4 flashes look more regular than real clicks

Modes 3 and 4 are deliberate trend indicators, not 1:1 click visualisations. Real radioactive decay follows a Poisson process so individual clicks are irregularly spaced; the trend pulse smooths this into an evenly-paced flash whose rate reflects activity vs baseline.

For per-click visual feedback, use mode 1 or 2, the [Pulse Out](/output/pulseout) module on any spare GPIO, or the dedicated blip LED on boards that fix the pin at build time via `GEIGER_BLIPLED` (ESPGeiger-HW).
