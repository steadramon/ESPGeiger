---
layout: page
title: CPM Mode
permalink: /configuration/cpm-mode
nav_order: 21
parent: Configuration
---

# CPM Calculation Mode

**CPM mode** picks how the displayed CPM is calculated. Set it on the [Config page](/configuration), Input tab, in the advanced settings. Changes apply at once, no restart.

## Picking a mode

* **Not sure?** Leave it on **Bucket** (the default).
* **Watching a hot source and want the fastest response?** Pick **Adaptive fast**.
* **Want the steadiest reading from a steady source?** Pick **Live ring**.
* **Want a fixed one-minute window for comparing devices?** Pick **Fixed 60s**.
* **Want a ring mode without the slow start after boot?** Pick **Fast start**.

## The five modes

| Value | Menu name | Behaviour |
|---|---|---|
| 0 | Fast start | Uses recent pulses just after boot, then blends into Bucket once a full window of data exists. |
| 1 | Live ring | Rate over the last <!-- gen:const PULSE_RING_SIZE -->256<!-- /gen --> pulses. Very smooth at background, quick at high rates, slow to follow change at very low rates. |
| 2 | Fixed 60s | Rate over the last <!-- gen:const FIXED_US us as=s -->60 seconds<!-- /gen -->, capped at <!-- gen:const FIXED_N -->64<!-- /gen --> pulses, so the window narrows above about <!-- gen:const FIXED_N -->64<!-- /gen --> CPM. Can read 0 if no pulse landed in the last minute. |
| 3 | Bucket *(default)* | Average of the per-second counts over the last <!-- gen:const GEIGER_CPM_COUNT s as=s -->60 seconds<!-- /gen -->. Stable, predictable, slowest to respond. |
| 4 | Adaptive fast | Rate over the last <!-- gen:const FAST_N -->19<!-- /gen --> pulses or <!-- gen:const FAST_US us -->5 seconds<!-- /gen -->, whichever is shorter. Fast, but jumps around at background rates. |

On serial builds for counters that only report CPM, Bucket uses the **CPM window** setting (default <!-- gen:const counter.cpm_window s short -->30 s<!-- /gen -->) instead of <!-- gen:const GEIGER_CPM_COUNT s as=s short -->60 s<!-- /gen -->. These counters already smooth their own CPM, so the display lags by roughly their window plus yours. See [Serial Counters](/hardware/serial#cpm-window-option).

## What changes when I switch modes

The mode changes the live CPM, CPS and dose shown to your own consumers:

* `/status` page top numbers
* OLED display
* Live serial output / GeigerLog connection
* MQTT (your own broker / Home Assistant)
* Webhook (your own URL)
* Local UDP broadcast between your ESPGeiger devices

Uploads to outside services always get a steady 60-second value, so the network sees consistent data from every device:

* **[Radmon](/output/radmon)** (radmon.org; with an interval over 90 s it gets the 5 or 15 minute average instead)
* **[ESPGeiger Stations](/output/webapi)** (stations.espgeiger.com)
* **[GMC / GeigerLog](/output/gmc)** protocol
* **[ThingSpeak](/output/thingspeak)** (your own channel)

The mode never affects the 5- and 15-minute averages, total and lifetime counts, the 24-hour history or the inter-pulse histogram.

## How reliable is a reading?

The Poisson nature of radiation means every reading has statistical
noise that shrinks as more pulses accumulate. The 95% confidence
interval as a percent is roughly `196 / sqrt(N)`, where N is the number
of pulses behind the reading. Quick reference:

| Pulses behind reading | Confidence | Meaning |
|---|---|---|
| 4 | ±98% | Mostly noise |
| 16 | ±49% | Noisy |
| 64 | ±24% | OK |
| 100 | ±20% | OK |
| 256 | ±12% | Solid |
| 1000+ | ±6% | Very solid |

At background rates, collecting ~256 pulses takes around 8 minutes;
at higher rates it happens much faster. Live ring and Fixed 60s tend to
give the most solid readings at low rates because they widen the
window to collect more pulses.

Details of the calculations, rate limits and memory use are on the [CPM mode internals](/dev/cpm-mode-internals) developer page.

## Cross references

* [Counter Configuration](/configuration/counter) - debounce and dead
  time settings that affect all modes.
* [HV Configuration](/configuration/hv) - high voltage and tube ratio
  settings that affect what counts as a pulse in the first place.
