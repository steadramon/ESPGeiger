---
layout: page
title: High Voltage (HV)
permalink: /configuration/hv
nav_order: 10
parent: Configuration
---

# HV Configuration

The **HV Config** page (`/hv`) appears on builds with an HV generator and voltage feedback: ESPGeiger-HW and the `esp8266_hv`, `esp8266oled_hv` and `esp32_hv` builds. Custom builds with an HV generator but no feedback show frequency and duty on the [Config page](/configuration) instead.

The HV stage is a Pulse-Width Modulated boost converter circuit. By controlling the PWM duty cycle we produce a stable HV DC driving voltage for the Geiger-Muller tube - typically ~400 V depending on tube spec.

![ESPGeigerHW HV](/img/espghv.png)

## HV Settings

The `/hv` page edits the `espghw` prefs:

<!-- gen:prefs espghw -->

| Setting | Key | Type | Default | Builds | Description |
|---|---|---|---|---|---|
| PWM Frequency | `espghw.freq` | Int 100-9000/40000/80000 | `6000` | all | PWM frequency in Hz. ESPGeiger-HW max 9000 Hz; `esp8266_hv` max 40000 Hz; `esp32_hv` max 80000 Hz. |
| PWM Duty | `espghw.duty` | Int 1-600/1023 | `280` | all | PWM duty (1-1023, 10-bit). Each step is ~0.1 % of period. |
| ADC VD Ratio | `espghw.ratio` | Int 0-50000 | `1035` | HV ADC builds | Voltage divider scale factor - converts ADC counts to displayed volts. Calibrate with an external HV meter. `0` disables the ADC readout. |
| ADC VD Offset | `espghw.offset` | Int -100 to 100 | `0` | HV ADC builds | Fixed offset added to the displayed voltage after scaling. |
| HV target | `espghw.target` | Int 0-500/700 | `0` | HV ADC builds | Auto-trim target voltage (0 = open loop, disabled). When non-zero, firmware trims duty by a few steps to hold target. See [Closed-Loop Trim](#closed-loop-trim). |

<!-- /gen -->

When changing settings:

- Always **disconnect the Geiger tube** before adjusting frequency or duty
- After submitting changes, the firmware briefly drops PWM low to avoid voltage spikes during the transition
- The closed-loop trim is suppressed for 30 seconds after changes, and for 5 minutes after boot while HV settles, so it doesn't chase the transient

## Protection

- ESPGeiger-HW caps duty at 600, well above its usual ~320, so a typo can't saturate the coil.
- ESPGeiger-HW drops duty to minimum if HV reads over 530 V for 3 seconds. It stays there until settings are saved or the device restarts, and `/hv` shows the cut-off.
- On ESP8266 the HV pin is held low if the PWM generator stops.

## Calibration with a Multimeter

> ⚠️ **HV is dangerous.** 400 V at the tube is well above the let-go threshold and the boost-cap can hold charge after PWM stops. Wear gloves where practical, keep one hand in your pocket, and discharge the output cap with a 1 MΩ resistor across it before touching the rail.

A standard digital multimeter has roughly **10 MΩ** input impedance. Probing the HV rail directly will load it down hard - you'll read maybe 200 V on a supply that's actually delivering 400 V into the tube - and the meter sees its full input rating rather than a scaled-down reading. The fix is a high-value series resistor in front of the meter that turns the meter input into the low side of a voltage divider.

**Recommended probe:** a single **1 GΩ resistor** in series with the meter's positive lead. With a typical 10 MΩ DMM the divider works out to:

```
V_HV / V_meter = (1 GΩ + 10 MΩ) / 10 MΩ ≈ 101
```

So **HV ≈ DMM reading × 100** (close enough - the 1% offset is well within tube tolerance). For a 10× reduction (e.g. for a high-impedance bench meter) use a 100 MΩ series resistor instead. If your DMM input impedance is something other than 10 MΩ, plug it into the formula above.

Build the probe on an offcut of perfboard or inside a marker-pen case - keep the resistor body well clear of any conductive surface, since 400 V across a 1 GΩ part will arc to anything close enough. Use HV-rated wire (silicone test-lead wire is fine) and finish with proper meter probes at the user end.

**Calibration procedure:**

1. **Disconnect the Geiger tube.** The tube is a current sink at threshold and will load the rail asymmetrically; calibrate against the bare boost output, not against the tube
2. Connect your DMM (with 1 GΩ series resistor) between the HV rail and ground, set to **DC volts, 20 V or 200 V range**
3. Open `/hv`. Set `Ratio = 1` and `Target = 0` (open-loop, no trim chasing)
4. Adjust `Duty` until the DMM reads close to the voltage you want (HV/100 - so 4.00 V for 400 V)
5. Note the **raw ADC counts** shown on `/hv` and your **measured HV** (DMM × 100)
6. Set `Ratio = measured_HV × 1024 / raw_ADC`. The displayed reading on `/hv` should now match your DMM × 100
7. Once `/hv` agrees with the DMM at one operating point, sweep duty over a couple of points to confirm the displayed voltage tracks the meter - if it does, the divider is calibrated
8. Reconnect the tube. Read the now-calibrated voltage on `/hv` directly - no probe needed for routine checks
9. (Optional) Set `Target` to your desired voltage to enable the closed-loop trim

If the displayed voltage drifts noticeably when you reconnect the tube, that's normal - tubes draw a few µA at threshold and most home-built boost stages have non-zero output impedance. Set the open-loop `Duty` to give your target voltage **with the tube connected**, then enable `Target` to track it through temperature and age.

## Generic HV Builds (esp8266_hv / esp32_hv)

1. Flash `esp8266_hv` or `esp32_hv` and wire your boost circuit (transistor, inductor, diode, cap, feedback divider). See [Build Targets](/install/buildtargets#hv-builds).
2. HV starts **inactive**. On the [Config page](/configuration), System tab, **HV Hardware**, set **HV PWM Pin** to the GPIO driving your transistor, save and **reboot**.
3. Open `/hv` to set frequency, duty and, after calibration, the target.

The feedback input is A0 on ESP8266 and GPIO 36 on ESP32.

## Closed-Loop Trim

With `Target` above 0, the firmware moves duty by 1 step every 10 seconds when the average of the last 8 readings is outside a band around the target, up to **±5 steps** from your saved `duty`. The band is 2 ADC counts either side, about ±2 V on ESPGeiger-HW. Set `duty` so HV is already right with the tube connected and `Target = 0`; the trim only tracks drift.

If the duty field shows `(trim +5)` or `(trim -5)`, the trim has hit its limit: move `duty` by hand in that direction until the trim sits near 0. The trim resets on reboot and on any duty or frequency change.