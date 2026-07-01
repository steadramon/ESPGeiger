---
layout: page
title: Pulse Out
permalink: /output/pulseout
parent: Outputs
nav_order: 12
---

# Pulse Out

A general-purpose per-pulse GPIO output. One module covers piezo, small
speaker via a transistor, LED, and attenuated line-out into a powered
speaker. Single edge or resonant burst, configurable polarity, all
non-blocking.

This is the lightweight cousin of the [Audio Tick](/output/audiotick)
output: no I2S, no amplifier IC, no synthesis. A real mechanical tick
per pulse, available on every ESP build.

Included in every shipped ESP build. Disabled by default; set the pin
and enable it from **Config > Pulse Out** after flashing.

## Settings

| Pref | Range | Default | Notes |
|---|---|---|---|
| `enable`   | 0 / 1     | `0`    | Takes effect on save, no reboot needed. |
| `pin`      | -1 to max | `-1`   | GPIO driving the load. `-1` keeps the pin idle. |
| `mode`     | 0 / 1 / 2 | `0`    | `0` = single pulse, `1` = resonant burst, `2` = LED fade. |
| `pulse_us` | 100-50000 | `500`  | Single-pulse width in microseconds. Sub-ms for audio, several ms for LEDs. |
| `freq`     | 1000-8000 | `3500` | Burst frequency in Hz. For a piezo, match its marked resonance. |
| `cycles`   | 1-10      | `3`    | Cycles per burst. More = louder + slightly longer. |
| `polarity` | 0 / 1     | `0`    | `0` = active high (most cases). `1` = active low (common-anode LEDs, inverted MOSFET drivers). |
| `fade_shift` | 2-4     | `3`    | Fade decay rate. `2` ≈ 100 ms total, `3` ≈ 250 ms, `4` ≈ 500 ms. |

Output is rate-limited to 20 clicks/second. At high CPS the audio thins
out while the counter still tracks every pulse.

## Per-device voice variation

Each device picks a small offset on `pulse_us` and `freq` based on its
chip ID, so two units next to each other sound subtly different rather
than identical.

## Use as an LED blink or fade

Same module, different load. Wire an LED (with current-limit resistor)
to the chosen GPIO and pick a mode:

* `mode=0`, `pulse_us=5000` to `20000` (5-20 ms): visible hard flash
  per click.
* `mode=2` (LED fade): full brightness on each click, then exponential
  decay via PWM. Looks like a Geiger-counter afterglow. `fade_shift`
  controls the decay rate (`2` ~100 ms, `3` ~250 ms, `4` ~500 ms).
* `polarity=0` if the LED's anode is on the GPIO side, `polarity=1` if
  the cathode is on the GPIO side (common-anode wiring).

Note: fade mode is **LED only**. Feeding PWM into a piezo or speaker
would produce a soft sustained tone, not a click.

Fade mode overlaps the built-in Blip LED. Both ship; pick whichever
fits your wiring. Blip LED lives on the board's dedicated pin,
Pulse Out on any GPIO you choose.

## Patterns

### Audio jack into a powered speaker

Use when you already have a desk speaker or aux input. Attenuate 3.3 V
down to roughly line level and AC-couple.

```
GPIO ---[R1: 10 kohm]---+---[C1: 1 uF]---o tip
                        |
                      [R2: 4.7 kohm]
                        |
                       GND --------------o sleeve
```

| Part | Why |
|---|---|
| R1 + R2 | Voltage divider: 3.3 V x 4.7 / (10 + 4.7) ~= 1.05 V, consumer line level. |
| C1 | Blocks the divider's DC bias so the line-in only sees the click transient. Non-polar 1 uF. |

Recommended settings: `mode=0`, `pulse_us=500`. The powered speaker's
amp shapes the click.

### Piezo disc or passive buzzer module

```
GPIO ----+---- Piezo lead 1
              Piezo lead 2 ---- GND
```

No extra parts. Volume tier depends on the piezo:

* Bare ceramic disc: wristwatch tick.
* Passive buzzer module (disc in a plastic Helmholtz cavity): distinct
  tick across a small room.

Active buzzers (the "5 V buzzer module" kind with an internal oscillator)
will not work in click mode. They can only buzz while powered. Look for
"passive" piezos.

Recommended settings:

* Quietest, sharpest: `mode=0`, `pulse_us=500`.
* Loudest at 3.3 V: `mode=1`, `cycles=3`, `freq=` whatever's marked on
  the piezo (typical: 2700, 3500, 4000 Hz).

### Small speaker via MOSFET

Use when you want room-filling clicks. The MOSFET sources current from a
5 V rail so the speaker isn't limited to what a GPIO can sink.

```
GPIO ---[220 ohm]--- Gate (2N7000)
                     |
                     Drain --- Speaker(-)
                               Speaker(+) --- +5 V
                                              ^
                     Drain ---[1N4148]--------+
                                  (cathode/band toward +5V)
                     Source --- GND
```

| Part | Purpose |
|---|---|
| **2N7000** (or BS170) | Logic-level N-channel MOSFET; switches cleanly from a 3.3 V GPIO. TO-92. |
| **220 ohm** | Gate-current limiter. |
| **1N4148** | Flyback diode across the speaker. Absorbs the inductive kick when the pulse turns off. |
| 4-8 ohm speaker | The driver. |

BJT alternative: BC337 NPN with a 1 kohm base resistor. The 2N3904 also
works but is quieter. Keep the flyback diode either way.

Recommended settings: `mode=1`, `cycles=2`, `freq=2500`. Tune `freq` to
taste; lower sounds woodier, higher sounds tinnier.

## Ideas

Some less obvious things you can do once Pulse Out is wired up.

### LED fade that reads as intensity

The `mode=2` exponential fade with `fade_shift=3` (default) is the
classic "ping and afterglow" look. Try `fade_shift=4` for a longer
decay (~500 ms). At high CPS the LED hovers near full brightness
because each click retriggers the fade; at background CPM it settles
between flashes.

### High-visibility tube indicator

Pair Pulse Out with a high-current LED (or a chain of LEDs through a
MOSFET driver) on a desk-mounted lamp. `mode=2`, `fade_shift=2` (~100
ms fade) on a 1 W LED makes a desk lamp react to background radiation
visibly across a room. Good for demos and classroom kit.

### Click-and-flash in sync

If you're already running [Audio Tick](/output/audiotick) on an ESP32,
also configure Pulse Out on a free GPIO with `mode=0`, `pulse_us=20000`.
Each pulse fires both at the same time, so the audible click and the
visible LED flash arrive together.

### Multi-station variation

Several ESPGeigers side by side (teaching kit, test fleet, sensor
array): give each one Pulse Out on an LED in `mode=2`. The per-device
voice variation gives each unit a slightly different fade decay and
click width, so the array doesn't flash in identical lockstep.

### Background-CPM piezo tick

Pair a low-CPS source with `mode=1`, `cycles=2`, `freq=4000` driving a
passive piezo. Even at background CPM (~30/min) the piezo fires a
clean short tick every couple of seconds, audible across a small room
without being intrusive.

### Headphone tick output

`mode=0`, `pulse_us=300-800` straight into the attenuator pattern (10
kohm + 4.7 kohm + 1 uF, see Patterns above) plugged into headphones
gives you a portable Geiger counter you can wear while the device sits
on a desk. Good for field walks if the tube has decent sensitivity.

## Comparison with Audio Tick

| | Pulse Out | [Audio Tick](/output/audiotick) |
|---|---|---|
| Platforms | ESP8266 + ESP32 | ESP32 only |
| Hardware  | Piezo, small speaker (via MOSFET), or line-out into a powered speaker | I2S amplifier IC + speaker |
| Sound     | Mechanical tick | Synthesised click with chirp + decay |
| Boot chime | None | 7 chimes + random |

Run both at once if you like. They share the same 20 clicks/sec cap.

## See also

* [Audio Tick](/output/audiotick) for I2S amplifier clicks with synthesis.
* [NeoPixel](/output/neopixel) for trend-coloured visual feedback.
* [Outputs index](/output)
