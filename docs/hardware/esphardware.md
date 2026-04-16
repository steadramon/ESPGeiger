---
layout: page
title: ESP Hardware
permalink: /hardware/esphardware
parent: Hardware
nav_order: 1
---

# ESPGeiger Compatible Hardware

ESPGeiger is written to be compatible with both the ESP8266 and ESP32 range of MCUs.

## ESP8266

The [Wemos D1 Mini](https://s.click.aliexpress.com/e/_DmfPg5L) (4MB) is the recommended ESP8266 MCU for ESPGeiger.

The ESP8266 ESPGeiger build is the base firmware for the official ESPGeiger-based hardware __ESPGeiger-HW__ and __ESPGeiger Log__.

## ESP32

### PCNT

The `ESP32` range of MCUs feature an in-built [hardware pulse counter](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/peripherals/pcnt.html) (`PCNT`). By default ESPGeiger uses the hardware PCNT on Pulse builds for ESP32 devices. A `no_pcnt` build is also available for ESP32 which uses the same Interrupt counter mechanism as the ESP8266 builds.

### PCNT Filter

The PCNT hardware includes a glitch filter that can ignore pulses shorter than a configurable threshold. This helps filter out electrical noise and interference that could otherwise be counted as false radiation events.

The filter value can be configured from the ESPGeiger web interface under __Config__. The value ranges from 0 to 1023, representing APB clock cycles at 80MHz (each unit = 12.5ns). A value of 0 disables filtering.

| Filter Value | Pulse Duration Filtered | Use Case |
|---|---|---|
| 0 | Disabled | No filtering |
| 100 (default) | < 1.25μs | Filters electrical noise while passing all real geiger pulses |
| 500 | < 6.25μs | More aggressive filtering for noisy environments |
| 1023 | < 12.8μs | Maximum filtering |

Real Geiger-Muller tube pulses are typically 50-200μs in duration, so even the maximum filter value will not affect real readings. The filter is only available on ESP32 PCNT builds.

The filter value can also be set at compile time with the `-D PCNT_FILTER=N` build flag. See [Build Options](/install/platformio#esp32-hardware-counter-pcnt) for details.

### Interrupt Debounce

On builds that use software interrupt counting instead of PCNT (all ESP8266 pulse builds, and ESP32 `no_pcnt` builds), a debounce window rejects any edge that arrives within a configurable number of microseconds of the previously accepted pulse. This serves the same purpose as the PCNT filter — suppressing electrical noise and contact bounce without affecting real tube pulses.

The debounce value can be configured from the ESPGeiger web interface under __Config__. The value is in microseconds, with a range of 0 to 10000. A value of 0 disables the debounce.

| Debounce Value | Pulses Rejected | Use Case |
|---|---|---|
| 0 | Disabled | No debounce |
| 500 (default) | < 500μs apart | Suppresses typical switching noise, passes all real pulses |
| 1000 | < 1ms apart | More aggressive for noisier setups |

Real Geiger-Muller tube dead time is around 50-200μs, so the default value comfortably accommodates real events. The debounce only applies to the software interrupt path; PCNT builds use the PCNT Filter above instead.

The default can also be set at compile time with the `-D GEIGER_DEBOUNCE=N` build flag. See [Build Options](/install/platformio#geiger-counter-input) for details.
