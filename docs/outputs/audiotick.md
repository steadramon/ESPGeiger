---
layout: page
title: Audio Tick
permalink: /output/audiotick
parent: Outputs
nav_order: 11
---

# Audio Tick Output

A per-pulse audible click through an I2S amplifier (e.g. MAX98357A) on
supported boards. A short boot chime plays once when the device starts
so you know the audio path is alive.

**ESP32 only.** AudioTick uses the ESP-IDF I2S driver, which is not
available on the ESP8266. See "Audible feedback on ESP8266" below for
the alternative.

## Shipped builds

| Env family | Hardware | I2S pins |
|---|---|---|
| `xh_s3e_*` | MINI ESP32-S3-N16R8 ("XH-S3E-AI") with on-board speaker | Fixed at BCLK=15, WS=16, DOUT=7 |
| `esp32s3_audio_*` | Generic ESP32-S3 + MAX98357A wired by you | Configurable in **Config > Audio Tick** (defaults BCLK=15, WS=16, DOUT=7) |

On the generic families, verify your wiring matches the pins in Config before
enabling. The XH-S3E envs have the pins locked to match the on-board speaker.

## Audible feedback on ESP8266

For per-pulse audio on an ESP8266 use [Pulse Out](/output/pulseout): single
GPIO into a piezo, or a small speaker via a MOSFET, or an attenuator into a
powered speaker. No I2S amplifier required.

## Settings (`tick` pref group)

<!-- gen:prefs tick -->

| Setting | Key | Type | Default | Builds | Description |
|---|---|---|---|---|---|
| Enable | `tick.enable` | Boolean | `1` on | all | Reboot to apply after enabling. |
| Volume | `tick.volume` | Int 0-100 | `60` | all | `0` mutes; click amplitude scales linearly. |
| Tone Hz | `tick.freq` | Int 300-6000 | `900` | all | Click centre frequency in Hz. |
| Decay ms | `tick.decay` | Int 2-100 | `4` | all | Decay time constant of the click body, in ms. |
| Engine | `tick.engine` | `0` Pool (4 variants), `1` Chirp (single voice) | `0` Pool (4 variants) | all | Click synthesis engine. |
| Peak % | `tick.peak` | Int 1-100 | `85` | all | Click amplitude ceiling (1-100). Lower = quieter and below NCN trip. |
| Boot chime | `tick.chime` | `0` Off, `1` Random | `1` Random | all | Boot chime: off, or a random pick. |
| I2S BCLK Pin | `tick.bclk` | Int 0-max GPIO | varies by build | boards without fixed pins | Reboot to apply |
| I2S WS Pin | `tick.ws` | Int 0-max GPIO | varies by build | boards without fixed pins | Reboot to apply |
| I2S DOUT Pin | `tick.dout` | Int 0-max GPIO | varies by build | boards without fixed pins | Reboot to apply |

<!-- /gen -->

## Throttle

Audio is rate-limited to 20 clicks/second. Above that the audio thins out
while the counter still tracks every pulse.

## Voice announcer

Audio-capable builds can speak the current reading aloud at a configurable
interval, using a 48-word ADPCM vocabulary (Cori, en_GB) baked into flash.
Each announcement reads CPM as a natural English number ("two hundred and
forty"), optionally followed by the dose ("twelve point three micro sievert
per hour", switching to milli sievert above 1000 µSv/h). When CPM is above
the warning or alert threshold the relevant state word prefixes the
announcement ("alert. two hundred and forty CPM."). When RadMon uploading
transitions state, the announcer can also say "RadMon online" or
"RadMon offline" on the next interval boundary.

The announcer obeys the audio quiet-hours window: nothing is spoken
between `klax.quiet_from` and `klax.quiet_to`.

### Settings (`klaxon` pref group)

<!-- gen:prefs klaxon -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Announce | `klaxon.announce_enable` | Boolean | `0` off | Master switch for periodic announcements. |
| Every (seconds) | `klaxon.announce_interval` | Int 10-3600 | `60` | Seconds between announcements. |
| Announce dose | `klaxon.announce_usv` | Boolean | `0` off | Append dose in µSv/h or mSv/h. |
| RadMon up/down | `klaxon.announce_radmon` | Boolean | `0` off | Speak RadMon online/offline transitions. |
| Volume | `klaxon.voice_volume` | Int 1-100 | `80` | Voice volume. Independent of the click volume. |

<!-- /gen -->

Test routes:

* `/adpcm?w=alert` plays a single vocabulary word
* `/adpcm?n=240` speaks a number

## Alert klaxon

Five klaxon patterns are synthesised on-device and fire automatically when
CPM crosses the alert threshold (rising edge only). A configurable cooldown
prevents repeated triggering during a sustained alert and the audio
quiet-hours window suppresses the klaxon at night.

| Pattern | Name | Sound |
|---|---|---|
| 0 | Alarm | Deep alternating high/low tones, reactor-style |
| 1 | Two-Tone | Police two-tone wail |
| 2 | Wail | Slow rising-and-falling air raid siren |
| 3 | Horn | Naval AOOGA horn |
| 4 | Voice | Cori says "alert" three times |

### Settings (`klax` pref group)

<!-- gen:prefs klax -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Klaxon on alert | `klax.enable` | Boolean | `0` off | Master switch. |
| Pattern | `klax.type` | `0` Alarm, `1` Two-Tone, `2` Wail, `3` Horn, `4` Voice | `0` Alarm | Pattern (see table above). |
| Volume | `klax.volume` | Int 1-100 | `55` | Klaxon volume. |
| Cooldown | `klax.cooldown` | Int 1-60 | `5` | Minutes to wait after a klaxon fires. |
| Quiet from | `klax.quiet_from` | Time HH:MM | (empty) | Start of the audio quiet-hours window. Silences the klaxon and the voice announcer. Blank = off. |
| Quiet to | `klax.quiet_to` | Time HH:MM | (empty) | End of the quiet window; crosses midnight if from > to. |

<!-- /gen -->

Test routes: `/klaxon` plays the configured pattern and `/klaxon?t=0` through
`/klaxon?t=4` the chosen one, immediately, ignoring the cooldown and quiet hours.

## See also

* [NeoPixel](/output/neopixel)
* [Outputs index](/output)
