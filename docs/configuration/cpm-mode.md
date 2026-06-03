---
layout: page
title: CPM Mode
permalink: /configuration/cpm-mode
nav_order: 21
parent: Configuration
---

# CPM Calculation Mode

Pulse-driven builds (and any test build that uses the pulse path) can pick
how the displayed CPM is calculated. The setting lives on the Config page
under **Pulse Input -> CPM mode** and takes effect immediately, no reboot.

Default is `3` (bucket). If you do not know what to pick, leave it at the
default. This page explains the trade-offs so you can change it on
purpose.

## The two underlying calculations

ESPGeiger keeps two measurement structures running at all times:

1. **Bucket** - a sliding mean of per-second pulse counts over a
   configurable window (60 seconds by default, 1-60 range). Fixed
   cadence. The legacy and default source for CPS and CPM.
2. **Pulse ring** - a 256 entry buffer of pulse timestamps, in
   microseconds. Each timestamp marks one real pulse. Only allocated when
   you pick a non-bucket mode, so default users pay 0 RAM for it.

Different modes read from one, the other, or blend them.

## The five modes

| Value | Name | Reads from | Window |
|---|---|---|---|
| 0 | auto | ring early, blend, bucket warm | adaptive |
| 1 | live | pulse ring (whatever it covers) | 0.3 s to 8 min |
| 2 | fixed_60s | ring entries younger than 60 s | exactly 60 s |
| 3 | bucket (default) | per-second mean over the bucket window | bucket window (60 s default) |
| 4 | adaptive_fast | last 19 pulses or 5 s, whichever shorter | dynamic |

### 0 - auto

At boot the ring is the only source with data (bucket is still warming).
Auto reads the ring until uptime exceeds the bucket window (60 seconds
by default), then linearly blends into the bucket over the next bucket
window, then it is pure bucket.

Good default for "I want something sensible without thinking about it",
but `3` is currently the default to avoid surprise on existing devices.

### 1 - live

Pure `(N-1) / T` from the ring. The window is whatever the ring covers
right now: lots of pulses at high rate (responsive), 256 pulses at low
rate (very smooth).

Best confidence interval at low rates (more N over a longer span), most
responsive at high rates. Stalest at very low rates: at 0.5 CPM the ring
might span 8 minutes and a rate change takes 8 minutes to reflect.

### 2 - fixed_60s

Walks the ring backwards counting entries with age below 60 seconds,
computes `(N-1) / T` over that window.

Useful if you want a strict "last minute" semantic for time-series
graphing or comparing devices. At very low rates may report 0 if no
pulses landed in the last 60 s.

### 3 - bucket (default)

Identical behaviour to all previous firmware versions. Sliding mean of
the last per-second pulse counts over the bucket window (60 seconds by
default), with dead-time correction applied as the value updates. This
path has no extra cost.

Stable, predictable, and the slowest to respond to a rate change. At low
rates the confidence interval is poor (~40% with 24 CPM background over
60 s) but the window is fixed so output module consumers can reason
about it.

### 4 - adaptive_fast

Walks back through the last 19 pulses **or** 5 seconds, whichever bound
hits first. Designed for spike hunting and quick visible response when
something changes.

At background (0.5 cps) the 19-pulse floor kicks in and the effective
window is about 38 s. At higher rates the 5 s cap takes over.

Best for users who want to see changes immediately. Noisy at low rates
because the small N gives a wide confidence interval.

## What changes when I switch modes

Mode changes the live CPM, CPS and dose values shown to your own
consumers - the things you point at the device:

* `/status` page top numbers
* OLED display
* Live serial output / GeigerLog connection
* MQTT (your own broker / Home Assistant)
* Webhook (your own URL)
* Thingspeak (your own channel)
* Local UDP broadcast (peer-to-peer between your ESPGeiger devices)

What it does **not** change - public fleet aggregators stay on a stable
60-second-window value so the network sees consistent data across every
device regardless of which mode anyone picked:

* **[Radmon](/output/radmon)** (radmon.org)
* **URADMonitor** (uradmonitor.com)
* **Safecast** (safecast.org)
* **WebAPI** (stations.espgeiger.com)
* **[GMC / GeigerLog](/output/gmc)** protocol

Also unaffected by mode regardless:

* The 5-minute and 15-minute smoothed averages (long-window smoothers
  stay long-window by design)
* Total click count and lifetime tracking
* 24-hour history bars
* Inter-pulse interval histogram

## Picking a mode

Quick guide:

* **You have not picked one before.** Leave at `3`. Read the rest of
  this page later.
* **You want responsiveness above all else, are watching a hot
  source.** Pick `4` (adaptive_fast).
* **You want the most stable reading available at a steady rate
  source.** Pick `1` (live) - more pulses, smaller confidence interval.
* **You want a strict 1 minute average for time series comparison.**
  Pick `2` (fixed_60s).
* **You like the legacy behaviour and do not want surprises.** Stay on
  `3`. Or move to `0` (auto) if you want adaptive without thinking.

## Memory cost

The pulse ring is 1 KB. It is **only allocated when you opt into a
non-bucket mode**. Default `3` users pay 0 RAM for the ring.

If you switch from `3` to anything else, the ring allocates on save and
stays allocated until reboot, even if you switch back to `3`. This avoids
heap fragmentation from repeated allocations.

If the device is out of heap when you switch modes the allocation fails
silently and the mode falls back to the bucket. Check `/info` heap
numbers if the new mode is not behaving.

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

At background rates, accumulating ~256 pulses takes around 8 minutes;
at higher rates it happens much faster. Modes 1 and 2 tend to give the
most solid readings at low rates because they widen the effective
window to collect more pulses.

## Cross references

* [Counter Configuration](/configuration/counter) - debounce and dead
  time settings that affect all modes.
* [HV Configuration](/configuration/hv) - high voltage and tube ratio
  settings that affect what counts as a pulse in the first place.
