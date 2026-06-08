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

## How the colour and flash rate are derived

The flash **rate** scales with the ratio of the current 1-minute CPM to the 5-minute average CPM (`CPM5`). When activity is rising relative to baseline the NeoPixel flashes faster; when activity is steady or falling it flashes slower. The interval is clamped to between 100 ms (≈10 flashes/sec) at maximum activity and 4 s at minimum.

The flash **colour** is derived from a *Poisson z-score* against the 5-minute baseline:

```
z = (CPM - CPM5) / sqrt(CPM5)
```

This is noise-aware - at low rates a small absolute change carries little statistical weight, while at high rates even a modest absolute change can be significant. The colour buckets:

| Condition | Colour | Meaning |
|---|---|---|
| `CPM < 0.01` and `CPM5 < 0.01` | __BLUE__ | No signal (no clicks observed) |
| `z > 3` | __RED__ | Significant rise (≥3σ above baseline) - likely a real source |
| `z > 1.5` | __YELLOW__ | Rising (≥1.5σ above baseline) - worth watching |
| `z < -1.5` | __PURPLE__ | Dropping (≥1.5σ below baseline) |
| _otherwise_ | __GREEN__ | Within normal variation around baseline |

If a [warning or alert threshold](/configuration/configuration#counter-settings) is configured and the current CPM crosses it, the NeoPixel colour switches to __YELLOW__ (warning) or __RED__ (alert) regardless of the z-score above.

## Why the flashes look more regular than real clicks

The NeoPixel pattern is a **trend indicator**, not a 1:1 click visualization. Real radioactive decay events follow a Poisson process so individual clicks are irregularly spaced; the NeoPixel deliberately smooths this into an evenly-paced flash whose rate reflects activity vs baseline. For per-click visual feedback, use the [Pulse Out](/output/pulseout) module on any spare GPIO (configure `pulse.pin` + `pulse.mode`), or the dedicated blip LED on boards that fix the pin at build time via `GEIGER_BLIPLED` (ESPGeiger-HW).
