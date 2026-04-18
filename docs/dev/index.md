---
layout: page
title: Testing & Development
permalink: /dev
has_children: true
nav_order: 50
---

# Testing & Development

Pull requests are welcomed! Please [visit GitHub](https://github.com/steadramon/ESPGeiger) if you would like to contribute to the project.

## Pulse Testing

![PWM module](img/pwmmodule.jpg)

PWM modules like the above have come in handy whilst developing the counting logic.

# Test Builds

ESPGeiger offers test builds specifically designed for ESP8266 and ESP32 microcontrollers. These builds enable the device to emulate various Geiger counter functionalities:

- __Pulse Counter__: Simulates the detection of radiation events by outputting a set number of pulses on the `TXPIN`
- __GC10 Serial Counter__: Emulates a GC10 Geiger counter, transmitting simulated radiation counts through the serial interface on the `TXPIN`
- __MightOhm Serial Counter__: Mimics a MightOhm Geiger counter, providing simulated radiation data in a MightOhm-compatible format via the serial port on the `TXPIN`

Simulated values follow a Poisson distribution, mimicking the random nature of real radioactive decay.

By default the Test output cycles through several ranges of reading, switch each 5 minute period:

- 0.5 CPS / 30 CPM
- 1 CPS / 60 CPM
- 1.66 CPS / 100 CPM
- 2 CPS / 120 CPM

A build option is available to count the number of CPM sent on the local device, note that if this option is enabled and the `RXPIN` and `TXPIN` of the same device are connected together, counted values will be doubled.

## Emulation and Communication

In these test build modes, you can connect the `TXPIN` (transmit pin) of one ESPGeiger device to the `RXPIN` (receive pin) of another ESPGeiger device, either the same unit or a different one. This setup allows you to create a closed-loop testing environment, mimicking real-world Geiger counter communication scenarios.

_Note_: No values are submitted to public services with Test builds.

## Tick Profiling

For diagnosing performance issues in the 1Hz tick callback, add `-DTICK_PROFILE` to `build_flags` in your environment config. Once per minute the device logs a breakdown of where time is spent:

```
Tick profile: total=425 ctr=230 wifi=109 mods=128 lps=58733
Mods max: mqtt=61 sdcard=25
```

- `total` — worst single tick duration in the last 60 seconds (us)
- `ctr` — max time in `gcounter.secondticker()`
- `wifi` — max time in WiFi tracking block
- `mods` — max total time in `EGModuleRegistry::tick_all()`
- `lps` — loop iterations per second (steady-state health indicator)
- `Mods max:` — per-module worst-case `s_tick` duration, slowest first

Zero overhead when the flag is not defined — all instrumentation compiles out.