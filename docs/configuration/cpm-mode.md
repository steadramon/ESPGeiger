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
under **Pulse Input -> Advanced -> CPM mode** and takes effect immediately, no reboot.

Default is `3` (bucket). If you do not know what to pick, leave it at the
default. This page explains the trade-offs so you can change it on
purpose.

## The two underlying calculations

ESPGeiger keeps two measurement structures running at all times:

1. **Bucket** - a sliding mean of per-second pulse counts. The window is
   60 seconds on Pulse, UDP and Test builds (not user-tunable). On
   Serial-CPM builds an `Input -> cpm_window` slider exposes 1-60 s with
   default 30 s. The legacy and default source for CPS and CPM.
2. **Pulse ring** - a 256 entry buffer of pulse timestamps, in
   microseconds. Each timestamp marks one real pulse. Only allocated when
   you pick a non-bucket mode, so default users pay 0 RAM for it.

Different modes read from one, the other, or blend them.

### Note on Serial CPM-every-second inputs

Most CPM-every-second serial protocols (GC10, MightyOhm CPM mode, etc.)
emit a number that has already been smoothed on the producer side -
typically a ~5 second rolling window. Our bucket then averages those
already-smoothed values over `cpm_window` seconds. This is **cascaded
smoothing**, not a bug: it stacks the producer's filter with ours,
trading response time for variance.

Effective response to a step change is roughly `producer_window +
cpm_window`. With defaults: ~5 s producer + 30 s bucket = ~35 s to
settle. If your producer is one of the rare lab devices doing a
full 60 s window, the bucket only adds smoothing on top - lower
`cpm_window` if you want snappier display.

## The five modes

| Value | Name | Reads from | Window |
|---|---|---|---|
| 0 | auto | ring early, blend, bucket warm | adaptive |
| 1 | live | pulse ring (whatever it covers) | 0.3 s to 8 min |
| 2 | bounded_60s | ring entries within 60 s, capped 64 pulses | 60 s at low rate, shrinks at high rate |
| 3 | bucket (default) | per-second mean over the bucket window | bucket window (see above) |
| 4 | adaptive_fast | last 19 pulses or 5 s, whichever shorter | 5 s at low rate, shrinks at high rate |

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

### 2 - bounded_60s

Walks the ring backwards taking the smaller of: entries within 60
seconds, or 64 entries. Computes `(N-1) / T` over that window.

At low rates (below ~64 CPM) the 64-pulse cap is irrelevant and you
get a true 60 s window. Above that the cap binds and the effective
window shrinks - at 1000 CPM you see roughly the last 4 s of data,
at 6000 CPM roughly the last 0.6 s. The reading stays correct, just
narrower-window than the name suggests.

The cap bounds how long the ring walk holds interrupts off, so
high-rate tubes do not stall their own ISR. At very low rates may
report 0 if no pulses landed in the last 60 s.

### 3 - bucket (default)

Identical behaviour to all previous firmware versions. Sliding mean of
the last per-second pulse counts over the bucket window, with dead-time
correction applied as the value updates. This path has no extra cost
and does not allocate the ring.

Stable, predictable, and the slowest to respond to a rate change. The
window is fixed per build:

* Pulse, UDP, Test: always 60 s (no pref)
* Serial CPS-capable producer: forced to 60 s
* Serial CPM-only: user-tunable 1-60 s via `cpm_window`, default 30 s

Output module consumers can reason about the window since it does not
change at runtime (other than from saving prefs).

### 4 - adaptive_fast

Walks back through the last 19 pulses **or** 5 seconds, whichever bound
hits first. Designed for spike hunting and quick visible response when
something changes.

* Below ~240 CPM the 5 s cap binds. You see only the handful of
  pulses that landed in the last 5 s - a 30 CPM tube gives ~3 pulses
  per 5 s, so the reading is very noisy at background.
* Around 240 CPM (19 pulses in ~5 s) the two caps cross.
* Above 240 CPM the 19-pulse cap binds and the window shrinks: at
  1900 CPM you see roughly the last 0.6 s; at 6000 CPM the last 0.2 s.

Best for users who want to see changes immediately at a hot source.
Will dance around violently at background rates - that is the price of
a 5-second window.

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
* **You want a bounded 1 minute window for time series comparison.**
  Pick `2` (bounded_60s). At rates under ~64 CPM this is a true 60 s
  window; above that it shrinks.
* **You like the legacy behaviour and do not want surprises.** Stay on
  `3`. Or move to `0` (auto) if you want adaptive without thinking.

### Do all five really earn their keep?

Honestly, modes overlap. If we were starting from scratch we might
ship only `3` (default, cheap), `4` (fast) and `1` (whole-ring
adaptive). The other two are kept because:

* `0` exists to remove the "warming" placeholder at boot for users
  who pick a ring mode and find the first 60 s frustrating.
* `2` exists to bound the ring walk and to give a name to "60 s if I
  can have it" - useful when comparing devices but mostly equivalent
  to `3` at typical background rates.

They are intentionally not removed because device prefs would silently
flip if we did. Pick what suits you; you do not need all five.

## Maximum trackable rate

None of the modes impose a CPM ceiling - the float CPS value has more
headroom than the hardware will ever reach. The real limits sit
elsewhere:

* **Debounce** (default 200 us on raw-interrupt pulse builds): caps
  the ISR at ~5000 CPS = 300 000 CPM. Pulses arriving inside the
  debounce window are dropped.
* **Dead time correction** (default 100 us on pulse): the correction
  formula saturates near `cps * dead_time = 0.875`, i.e. ~8750 CPS =
  525 000 CPM. Above that the tube is paralysed in any case.
* **PCNT** (ESP32 with pulse counter): bounded by the input filter, not
  by ISR throughput - typically higher headroom than interrupt mode.
* **Interrupt saturation guard** (raw-interrupt builds): if the raw
  ISR-entry rate exceeds 10 000/sec (way above any tube, easy for a
  floating pin or noisy cable to hit) the input is parked for 5 s and
  the rest of the firmware keeps running. See [Interrupt Saturation
  Guard](/hardware/esphardware#interrupt-saturation-guard).

Practical fleet limit is whichever of debounce or dead-time you hit
first. None of the CPM modes notice; they just report what the ring or
bucket saw.

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
