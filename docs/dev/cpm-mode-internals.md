---
layout: page
title: CPM Mode Internals
permalink: /dev/cpm-mode-internals
nav_order: 30
parent: Testing & Development
---

# CPM Mode Internals

Implementation notes for the [CPM mode](/configuration/cpm-mode) setting (`counter.cpm_mode`, default `3`).

## The two underlying calculations

1. **Bucket** - a sliding mean of per-second pulse counts. The window is
   60 seconds on Pulse, UDP and Test builds and on serial builds whose
   counter reports CPS (MightyOhm). On CPM-only serial builds the
   `counter.cpm_window` pref sets it, 1-60 s, default 30 s.
2. **Pulse ring** - a 256-entry buffer of pulse timestamps in
   microseconds. PCNT, serial and UDP inputs feed it with synthetic
   spacing. Only allocated when a non-bucket mode is selected.

| Value | Internal name | Reads from | Window |
|---|---|---|---|
| 0 | auto | ring early, blend, bucket warm | adaptive |
| 1 | live | pulse ring (whatever it covers) | 0.3 s to 8 min |
| 2 | bounded_60s | ring entries within 60 s, capped 64 pulses | 60 s at low rate, shrinks at high rate |
| 3 | bucket | per-second mean over the bucket window | bucket window |
| 4 | adaptive_fast | last 19 pulses or 5 s, whichever shorter | 5 s at low rate, shrinks at high rate |

* **auto** reads the ring until uptime exceeds the bucket window, then
  blends linearly into the bucket over the next window.
* **live** is `(N-1) / T` over the whole ring. At 0.5 CPM the ring can
  span 8 minutes.
* **bounded_60s** walks the ring backwards, taking entries within 60 s
  or 64 entries, whichever is fewer. At 1000 CPM that is ~4 s of data,
  at 6000 CPM ~0.6 s. The cap bounds how long the walk holds
  interrupts off.
* **bucket** applies dead-time correction as the value updates and does
  not allocate the ring.
* **adaptive_fast**: below ~240 CPM the 5 s cap binds; above it the
  19-pulse cap binds (1900 CPM ~0.6 s, 6000 CPM ~0.2 s).

Mode `0` exists to hide the warm-up placeholder at boot for ring users;
mode `2` is mostly equivalent to `3` at background rates. Both are kept
so existing device prefs do not change meaning.

## Cascaded smoothing on CPM-only serial inputs

Most CPM-every-second serial protocols (GC10, MightyOhm CPM mode, etc.)
already smooth on the producer side, typically over ~5 s. The bucket
then averages those values over `cpm_window` seconds. Effective
response to a step is roughly `producer_window + cpm_window`: with
defaults ~5 s + 30 s = ~35 s.

## Maximum trackable rate

None of the modes impose a CPM ceiling. The real limits:

* **Debounce** (default 200 us on interrupt pulse builds): caps the
  ISR at ~5000 CPS = 300 000 CPM.
* **Dead time correction** (default 100 us on pulse builds): the
  formula saturates near `cps * dead_time = 0.875`, ~8750 CPS =
  525 000 CPM. Above that the tube is paralysed in any case.
* **PCNT** (ESP32): bounded by the input filter, not ISR throughput.
* **Interrupt saturation guard** (interrupt builds): above 10 000 ISR
  entries/sec the input is parked for 5 s. See
  [Troubleshooting](/troubleshooting#log-shows-isr-storm).

## Memory cost

The pulse ring is 1 KB, allocated only when a non-bucket mode is
selected. Once allocated it stays until reboot, even after switching
back to `3`, to avoid heap fragmentation. If the allocation fails the
mode falls back to bucket silently; check `/info` heap numbers.
