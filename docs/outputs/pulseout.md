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

<!-- gen:prefs pulse -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Enable | `pulse.enable` | Boolean | `0` off | Takes effect on save, no reboot needed. |
| GPIO | `pulse.pin` | Int -1 to max GPIO | `-1` | GPIO driving the load. `-1` keeps the pin idle. |
| Polarity | `pulse.polarity` | `0` Active high, `1` Active low | `0` Active high | Active high suits most cases. Active low is for common-anode LEDs and inverted MOSFET drivers. |
| Mode | `pulse.mode` | `0` Pulse, `1` Burst, `2` LED fade | `0` Pulse | Single pulse, resonant burst (for a piezo or speaker) or LED fade. |
| Pulse width ms | `pulse.pulse_ms` | Int 1-50 | `5` | Single-pulse width in milliseconds. 1 ms for audio, several ms for LEDs. |
| Burst Hz | `pulse.freq` | Int 1000-8000 | `3500` | Burst frequency in Hz. For a piezo, match its marked resonance. |
| Burst cycles | `pulse.cycles` | Int 1-10 | `1` | Cycles per burst. More = louder + slightly longer. |
| Fade rate | `pulse.fade_shift` | `0` Fast, `1` Medium, `2` Slow | `1` Medium | Fade decay rate: Fast is ~100 ms, Medium ~250 ms, Slow ~500 ms. |
| Brightness | `pulse.brightness` | Int 0-100 | `100` | LED brightness in percent (LED loads only). Applies to Pulse and Fade modes. |
| Max Hz | `pulse.max_hz` | Int 0-200 | `20` | Cap on clicks per second. `0` removes the cap. |
| Quiet from | `pulse.quiet_from` | Time HH:MM | (empty) | Silence clicks from this time. Blank = off. |
| Quiet to | `pulse.quiet_to` | Time HH:MM | (empty) | End of the quiet window; crosses midnight if from > to. |

<!-- /gen -->

Output is rate-limited to `max_hz` clicks per second, 20 by default. At high
CPS the audio thins out while the counter still tracks every pulse.

## Driving another counter

Pulse Out is an indicator, not a count relay. At the default `max_hz` of 20 a
downstream counter stops seeing new pulses above **1200 CPM** and reads a flat
line, not an error.

To relay counts instead, set `max_hz` to `0` and `pulse_ms` to `1`. The output
then follows every count up to roughly **500 per second (30,000 CPM)**, above
which a new count arriving mid-pulse is dropped.

Two things to know before relying on it:

- Per-device voice variation jitters `pulse_ms` by a few percent (see below).
  Harmless for a counter reading edges, wrong if the receiver measures width.
- Quiet hours silence the output. A relay set up inside a quiet window looks
  like a dead tube at the far end.

## Per-device voice variation

Each device picks a small offset on `pulse_ms` and `freq` based on its
chip ID, so two units next to each other sound subtly different rather
than identical.

## Use as an LED blink or fade

Same module, different load. Wire an LED (with current-limit resistor)
to the chosen GPIO and pick a mode:

* `mode=0`, `pulse_ms=5` to `20`: visible hard flash
  per click.
* `mode=2` (LED fade): full brightness on each click, then exponential
  decay via PWM. Looks like a Geiger-counter afterglow. `fade_shift`
  controls the decay rate (`0` ~100 ms, `1` ~250 ms, `2` ~500 ms).
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

Recommended settings: `mode=0`, `pulse_ms=1`. The powered speaker's
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

* Quietest, sharpest: `mode=0`, `pulse_ms=1`.
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

- **Glowing LED**: `mode=2` fades the LED after each click. Try
  `fade_shift=2` for a longer afterglow; drive a 1 W LED through a
  MOSFET for a desk lamp that reacts across a room.
- **Click and flash together**: with [Audio Tick](/output/audiotick)
  running, add Pulse Out on a spare GPIO with `mode=0`, `pulse_ms=20`.
- **Headphones**: `mode=0`, `pulse_ms=1` into the attenuator pattern
  above (10 kohm + 4.7 kohm + 1 uF) gives a tick you can listen to on
  headphones.

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
