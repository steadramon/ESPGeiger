---
layout: page
title: Counter
permalink: /configuration/counter
nav_order: 20
parent: Configuration
---

# Counter Configuration

Two settings on the Config page that often get confused: **Debounce** and **Tube dead time**. They are related but serve different jobs, and getting them right matters at high count rates.

## Debounce

Debounce is a hardware level filter. It runs inside the input ISR and drops any pulse that arrives within the configured window of the previous pulse.

The job of debounce is to reject electrical artifacts: tube ringing, capacitive pickup, amplifier overshoot. These are not real radiation events and should not be counted.

| Setting | Default | Range |
|---|---|---|
| `input.debounce` | 200 µs | 0 - 10000 µs |

If your front end is clean (well filtered amplifier, short cable runs), 100 µs is usually safe. If you see suspiciously high background CPM that drops sharply with shielding, your debounce may be too low and you are counting noise.

If you suspect real pulses are being dropped at high rates (you flash a hot source and the count flattens out at a too-low value), debounce may be too high.

## Tube dead time

Tube dead time is a physical property of the GM tube itself. After each ionisation event the tube needs to recover before it can fire again. During that window real radiation events are missed by the tube hardware, not by the firmware.

ESPGeiger applies a statistical upward correction to the displayed CPS to compensate for what the tube missed. The correction kicks in only above 50 CPS (3000 CPM); at typical background rates it has no observable effect.

| Setting | Default | Range |
|---|---|---|
| `input.dead_time_us` | 0 µs (disabled) | 0 - 1000 µs |

Set this to the value from your tube's datasheet. A short list of common tubes:

| Tube | Dead time |
|---|---|
| LND-7317 | ~30 µs |
| SBM-20 | ~90 µs |
| LND-712 | ~90 µs |
| SI-3BG | ~90 µs |
| J305 | ~50-100 µs |
| M4011 | ~150 µs |
| SBM-20-1 | ~1500 µs |

Setting `0` disables the correction entirely.

## When debounce and dead time interact

Debounce is a filter (drops pulses inside the ISR), dead time is a correction (statistical adjustment to the displayed rate). They are not the same thing and should not be merged.

The recommended rule of thumb:

* Set dead time from the tube datasheet. This is fixed physics.
* Set debounce as low as your front end allows without false counts. This depends on your hardware.

A reasonable setup has `debounce <= dead_time + a small margin`. If your debounce is much larger than your dead time, you are filtering pulses the tube did produce, and the dead time correction will not recover them because it assumes the only thing lost was tube recovery.

If you need a large debounce to reject hardware noise, that is a sign the front end could be improved (better shielding, lower impedance, an output buffer on the tube amplifier).

## References

* Tube datasheets - search for "<tube name> datasheet" and look for "dead time", "recovery time", or "resolving time".
* [Wikipedia: Dead time](https://en.wikipedia.org/wiki/Dead_time)
