---
layout: page
title: High Voltage (HV)
permalink: /configuration/hv
nav_order: 10
parent: Configuration
---

# HV Configuration

__Note__: The dedicated `/hv` page is available on builds with both `-D ESPG_HV` and `-D ESPG_HV_ADC` set (i.e. PWM HV generator *and* a real ADC feedback divider for live voltage readout). The ESPGeiger-HW kit ships with both enabled, as do the generic `esp8266_hv` and `esp32_hv` builds.

Builds with only `-D ESPG_HV` (PWM generator without feedback) expose the same `freq`/`duty` controls in the regular Config page rather than on a dedicated /hv page, since there's no live readback to justify a real-time view.

The HV stage is a Pulse-Width Modulated boost converter circuit. By controlling the PWM duty cycle we produce a stable HV DC driving voltage for the Geiger-Muller tube — typically ~400 V depending on tube spec.

![ESPGeigerHW HV](/img/espghv.png)

## HV Settings

The `/hv` page exposes:

| Setting | Description |
|---|---|
`Frequency` | PWM frequency in Hz. ESPGeiger-HW max 9000 Hz; `esp8266_hv` max 40000 Hz; `esp32_hv` max 80000 Hz.
`Duty` | PWM duty (1-1023, 10-bit). Each step is ~0.1 % of period.
`Ratio` | Voltage divider scale factor — converts ADC counts to displayed volts. Calibrate with an external HV meter.
`Target` | Auto-trim target voltage (0 = open loop, disabled). When non-zero, firmware compensates ±8 LSB to hold target.

When changing settings:

- Always **disconnect the Geiger tube** before adjusting frequency or duty
- After submitting changes, the firmware briefly drops PWM low to avoid voltage spikes during the transition
- The closed-loop trim is suppressed for 30 seconds after changes so it doesn't chase the transient

## Generic HV Builds (esp8266_hv / esp32_hv)

These builds compile in the same HV management used on ESPGeiger-HW, but on stock ESP8266 or ESP32 boards. To use:

1. Flash the appropriate `esp8266_hv` or `esp32_hv` firmware
2. Wire your boost converter circuit (transistor, inductor, output diode + cap, feedback divider)
3. On first boot, HV is **inactive** (PWM pin defaults to -1) — no pin is driven
4. Open `/egprefs`, find the **HV Hardware** section
5. Set the **HV PWM Pin** to the GPIO connected to your transistor base/gate
6. Save and **reboot** — HV will start up on that pin
7. Open `/hv` to set frequency, duty, and (after calibration) the auto-trim target

The ADC feedback pin is set per-build:

- ESP8266: hardware-fixed at A0 (only ADC pin available)
- ESP32: defaults to **GPIO 36** (SVP / ADC1_CH0), input-only and ADC1 so no WiFi conflict. Override at build time with `-DGEIGER_VFEEDBACKPIN=NN`.

## Closed-Loop Trim

When `Target` is set to a non-zero voltage, the firmware automatically nudges duty by ±1 LSB every 10 seconds to hold actual HV near the target. The trim is bounded to ±8 LSB so a feedback fault can't push HV more than ~±8 V from the configured open-loop point.

If you see the trim rail at its bound (visible as `(trim +8)` or `(trim -8)` next to the duty field), your `duty` pref is mis-calibrated — adjust it manually toward the rail direction until trim sits near 0.

The trim resets to 0 on reboot and on any duty/frequency change via `/hv`.