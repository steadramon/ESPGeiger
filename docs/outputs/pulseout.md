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

## Build flag

Enabled with `-DPULSE_OUT`, which is set in the common `[com-esp]` block
so every shipped ESP build includes it. The module is disabled by
default (`pulse.enable=0`); set the pin and enable it from the **Config
> pulse** page after flashing.

Cost on ESP8266 is ~360 B RAM + ~2.2 KB flash; on ESP32 ~56 B RAM +
~2.2 KB flash.

## Settings (`pulse` pref group)

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

Output is rate-limited to 20 clicks/second through a 5-token bucket. At
high CPS the audio thins out while the underlying counter still tracks
every pulse.

## Non-blocking

Two different mechanisms keep clicks off the main loop:

* Single pulse and LED fade run on a state machine polled at 1 ms cadence.
  `notifyClick()` flips the pin and registers a deadline; the registry
  loop picks up the trailing edge on a later tick.
* Resonant burst uses Arduino's `tone()`, which schedules a hardware
  timer (timer1 on ESP8266, LEDC on ESP32). Microsecond-precise transitions
  at any audible frequency, also fully non-blocking.

This matters for longer pulses (e.g. 5-20 ms LED flashes) and for high
CPS, where a blocking implementation would tie up the loop and starve
other modules.

## Per-device voice variation

Each device picks a small offset on `pulse_us` and `freq` based on its
chip ID. Two units next to each other sound subtly different rather
than identical. Computed once at boot.

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

The fade mode uses `analogWrite` for PWM. On ESP8266 the PWM frequency
is set globally; if HV is enabled it picks HV's frequency (smooth), if
not it defaults to 1 kHz (works but a careful eye may see slight
flicker on the brightest steps). On ESP32 the LEDC channel handles this
natively.

Note: fade mode is **LED only**. Feeding PWM into a piezo or speaker
would produce a soft sustained tone, not a click.

This overlaps the existing runtime-configurable blip LED. The blip LED
uses JLed for asynchronous scheduling on a build-flag-fixed or
runtime-set pin; Pulse Out uses the state machine described above. Both
ship; pick whichever fits your build.

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

## Ideas: giving your Geiger counter a voice

The patterns above cover the basics. Some less obvious things you can
do once Pulse Out is wired up:

### Make the LED feel like radiation

The `mode=2` exponential fade with `fade_shift=3` (the default) is
already the classic "ping and afterglow" look that old physical Geiger
counters had. Try `fade_shift=4` for a longer, dreamier decay (~500
ms). At high CPS the LED hovers near full brightness because each click
retriggers the fade; at background CPM the LED settles between
flashes. The result reads as "intensity" without any sigma calculation.

### High-visibility tube indicator

Pair Pulse Out with a high-current LED (or a chain of LEDs through a
MOSFET driver) on a desk-mounted lamp. `mode=2`, `fade_shift=2` (~100
ms fade) on a 1 W LED makes a desk lamp react to background radiation
visibly across a room. Useful for demos and classroom kit.

### Click-and-flash in sync

If you're already running [Audio Tick](/output/audiotick) on an ESP32,
also configure Pulse Out on a free GPIO with `mode=0`,
`pulse_us=20000`. Each pulse fires both at the same time, so the
audible click and the visible flash arrive together. Cheap "stereo
feedback" that costs one LED and one resistor.

### Multi-station "wave"

If you have several ESPGeiger units side by side (a teaching kit, a
fleet of test stations, a sensor array), give each one Pulse Out on an
LED in `mode=2`. The per-device voice variation means their fades have
slightly different decay characters and click widths, so the array
flickers asymmetrically rather than all flashing in identical lockstep.
A small detail, but it makes a group of units look alive instead of
identical.

### Telltale "I am alive" pip

Pair a low-CPS source with `mode=1`, `cycles=2`, `freq=4000` driving a
passive piezo. Even at background CPM (~30/min) the piezo fires a
clean short tick every couple of seconds, audible across a small room
without being intrusive. Drop the buzzer into the case as a soft
heartbeat for the device.

### Headphone tick output

`mode=0`, `pulse_us=300-800` straight into the attenuator pattern (10
kohm + 4.7 kohm + 1 uF, see Patterns above) plugged into headphones
gives you a portable Geiger counter you can wear with the device
sitting on a desk. Great for field walks if your tube has decent
sensitivity.

### Door / motion-sensor feel

Wire Pulse Out as `mode=2` LED fade into a window-mounted bright LED
or a strip of NeoPixels (drive them through a level shifter). The
afterglow plus the per-device voice character makes the system feel
like a science-fiction prop rather than a piece of test equipment.
Lean into it.

## Comparison with Audio Tick

| | Pulse Out | [Audio Tick](/output/audiotick) |
|---|---|---|
| Platforms | ESP8266 + ESP32 | ESP32 only |
| Hardware  | 1 piezo, or 1 piezo + MOSFET + speaker, or attenuator + powered speaker | I2S amplifier IC + speaker |
| Sound     | Mechanical tick | Synthesised click with chirp + decay |
| Boot chime | None | 7 chimes + random |
| CPU cost  | Negligible (a few digitalWrites per click, non-blocking) | Moderate (per-sample synthesis, throttled) |

Run both at once if you like; they share the per-pulse hook and the
20 clicks/sec throttle.

## See also

* [Audio Tick](/output/audiotick) for I2S amplifier clicks with synthesis.
* [NeoPixel](/output/neopixel) for trend-coloured visual feedback.
* [Outputs index](/output)
