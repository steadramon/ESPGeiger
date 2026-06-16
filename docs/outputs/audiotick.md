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

## Build flag and envs

Enabled with `-DAUDIO_TICK`. Shipped in two env families:

| Env | Hardware | Pins fixed at compile time? |
|---|---|---|
| `xh_s3e_pulse`, `xh_s3e_serial`, `xh_s3e_udp`, `xh_s3e_test` | MINI ESP32-S3-N16R8 (sold as "XH-S3E-AI") with on-board speaker | Yes (BCLK=15, WS=16, DOUT=7) |
| `esp32oled_audio_pulse`, `esp32oled_audio_pulse_no_pcnt`, `esp32oled_audio_serial`, `esp32oled_audio_test` | Generic ESP32 + OLED, MAX98357A wired by the user | No, configurable at runtime |

The XH-S3E envs hard-block the pin prefs with `-DAUDIO_TICK_PINS_BLOCKED`
because that board's I2S routing is fixed. On the generic ESP32 + OLED
envs you set the pins in the `tick` pref group after the first boot.

## Pins

| Build flag | Role | XH-S3E (N16R8) default | esp32oled_audio default |
|---|---|---|---|
| `AUDIO_TICK_BCLK` | I2S bit clock        | `15` | `26` |
| `AUDIO_TICK_WS`   | I2S word select      | `16` | `25` |
| `AUDIO_TICK_DOUT` | I2S data out to DIN  | `7`  | `22` |

On the `esp32oled_audio_*` envs the build-flag values are just defaults;
the runtime prefs (`tick.bclk` / `tick.ws` / `tick.dout`) override them
on next boot. Verify your wiring matches before enabling.

## Audible feedback on ESP8266

The chirp/pool engines depend on the ESP-IDF I2S driver and don't port to
the ESP8266. For per-pulse audio on an ESP8266 use [Pulse Out](/output/pulseout):
single GPIO into a piezo, or a small speaker via a MOSFET, or an
attenuator into a powered speaker. Same per-pulse hook, non-blocking
state machine, no I2S amplifier required.

## Settings (`tick` pref group)

| Pref | Range | Default | Notes |
|---|---|---|---|
| `enable` | 0 / 1 | `0` | Reboot to apply after enabling. |
| `volume` | 0-100 | `60` | `0` mutes; click amplitude scales linearly. |
| `freq`   | 300-6000 Hz | `900` | Click centre frequency. |
| `decay`  | 2-100 ms | `4` | Decay time constant of the click body. |
| `engine` | 0 / 1 | `0` | `0` = Pool, `1` = Chirp. |
| `chime`  | 0-8 | `1` | `0` = silent boot, `1` = random pick, `2`-`8` = specific. |

## Throttle

Audio is rate-limited to 20 clicks/second through a 5-token bucket. Above
that the audio thins out while the underlying counter still tracks every
pulse. Background CPM keeps the bucket full.

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
between `tick.aq_from` and `tick.aq_to`.

### Settings (`voice` pref group)

| Pref | Range | Default | Notes |
|---|---|---|---|
| `vol` | 0-100 | `80` | Voice volume. Independent of the click volume. |
| `announce_enable` | 0 / 1 | `0` | Master switch for periodic announcements. |
| `announce_interval` | 10-3600 s | `60` | Seconds between announcements. |
| `announce_usv` | 0 / 1 | `0` | Append dose in µSv/h or mSv/h. |
| `announce_radmon` | 0 / 1 | `0` | Speak RadMon online/offline transitions. |

Test routes:

* `/say?w=alert` plays a single vocabulary word
* `/say?w=two,hundred,and,forty` plays a comma-separated sequence
* `/announce` triggers the current announcement immediately

## Alert klaxon

Five klaxon patterns are synthesised on-device and fire automatically when
CPM crosses the alert threshold (rising edge only). A configurable cooldown
prevents repeated triggering during a sustained alert, and the audio
quiet-hours window suppresses the klaxon at night.

| Pattern | Name | Sound |
|---|---|---|
| 0 | Alarm | Deep alternating high/low tones, reactor-style |
| 1 | Two-Tone | Police two-tone wail |
| 2 | Wail | Slow rising-and-falling air raid siren |
| 3 | Horn | Naval AOOGA horn |
| 4 | Voice | Cori says "alert" three times |

### Settings (`klaxon` pref group)

| Pref | Range | Default | Notes |
|---|---|---|---|
| `klax_enable` | 0 / 1 | `0` | Master switch. |
| `klax_type` | 0-4 | `0` | Pattern (see table above). |
| `klax_vol` | 0-100 | `55` | Klaxon volume. |
| `klax_cd` | 1-60 min | `5` | Cooldown after a klaxon fires. |
| `aq_from`, `aq_to` | HH:MM | (off) | Audio quiet-hours window. |

Test routes: `/klaxon?t=0` through `/klaxon?t=4` play the chosen pattern
immediately, bypassing the cooldown but still respecting quiet hours.

## See also

* [NeoPixel](/output/neopixel)
* [Outputs index](/output)
