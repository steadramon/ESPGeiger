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
| PWM frequency | `espghw.freq` | Int 100-6000/9000/40000/78000 | `6000` | all | PWM frequency in Hz. ESPGeiger-HW max 9000 Hz; `esp8266_hv` max 40000 Hz; `esp32_hv` max 78000 Hz. |
| PWM duty | `espghw.duty` | Int 1-282/600/1023 | varies by build | all | PWM duty (1-1023, 10-bit). Each step is ~0.1 % of period. ESPGeiger-HW caps it at 600. Every build leaves the gate low at least 4 us a period, so on `esp32_hv` the most duty is 998 at 6000 Hz. |
| Divider ratio | `espghw.ratio` | Int 0-50000 | varies by build | HV ADC builds | Voltage divider scale factor - converts the ADC reading to displayed volts. The reading is in mV on every build (calibrated mV on ESP32 builds, the ESP8266 count is about 1 mV), so the same divider takes about the same ratio on any chip. Calibrate with an external HV meter. `0` disables the ADC readout. |
| Divider offset | `espghw.offset` | Int -100 to 100 | `0` | HV ADC builds | Fixed offset added to the displayed voltage after scaling. |
| HV target | `espghw.target` | Int 0-450/500/700 | `0` | HV ADC builds | Auto-trim target voltage (0 = open loop, disabled). When non-zero, firmware trims duty by a few steps to hold target. See [Closed-Loop Trim](#closed-loop-trim). |

<!-- /gen -->

When changing settings:

- Always **disconnect the Geiger tube** before adjusting frequency or duty
- After submitting changes, the firmware briefly drops PWM low to avoid voltage spikes during the transition
- The closed-loop trim is suppressed for 30 seconds after changes and for 5 minutes after boot while HV settles, so it doesn't chase the transient

## Protection

- ESPGeiger-HW caps duty at 600, well above its usual ~320, so a typo can't saturate the coil.
- ESPGeiger-HW drops duty to minimum if HV reads over 530 V for 3 seconds. It stays there until settings are saved or the device restarts and `/hv` shows the cut-off.
- The hidden `espghw.ov_v` (below) can lower that limit, never raise it.
- `esp8266_hv`, `esp8266oled_hv` and `esp32_hv` have no over-voltage cut-off until you set `espghw.ov_v`.
- On ESP8266 the HV pin is held low if the PWM generator stops.

### Hidden settings

These don't show on `/hv` or the Config page. Set them over [serial](/output/serial) or the Status-page command box, for example `set espghw.ov_v 500`. They are on every build with HV feedback.

| Key | Default | Range | Notes |
|---|---|---|---|
| `espghw.ov_v` | 530 on ESPGeiger-HW, else `0` | 0-2000 | Over-voltage cut-off in volts, `0` = off. On ESPGeiger-HW `0` or anything over the board's limit means that limit. |
| `espghw.ov_s` | `3` | 1-60 | Seconds over `ov_v` before the cut-off. |
| `espghw.trim_max` | `5` | 0-20 | Most steps the closed-loop trim moves either side of the saved duty. |
| `espghw.trim_s` | `10` | 8-120 | Seconds between trim steps. |
| `espghw.settle_s` | `30` | 0-600 | No trim for this long after a change. |
| `espghw.boot_s` | `300` | 0-1800 | No trim for this long after boot. |

## Calibration with a Multimeter

> ⚠️ **HV is dangerous.** 400 V at the tube is well above the let-go threshold and the boost-cap can hold charge after PWM stops. Wear gloves where practical, keep one hand in your pocket and discharge the output cap with a 1 MΩ resistor across it before touching the rail.

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
3. Open `/hv`. Leave `Ratio` as it is (the build default is a good start), set `Offset = 0` and `Target = 0` (open-loop, no trim chasing)
4. Adjust `Duty` until the DMM reads close to the voltage you want (HV/100 - so 4.00 V for 400 V)
5. Wait about 10 seconds, then note the voltage **shown** on `/hv` and your **measured HV** (DMM × 100)
6. Set `Ratio = Ratio × measured_HV / shown_HV`, rounded. The shown voltage moves by the same factor, so it should now match your DMM × 100.
7. Once `/hv` agrees with the DMM at one operating point, sweep duty over a couple of points to confirm the displayed voltage tracks the meter - if it does, the divider is calibrated
8. Reconnect the tube. Read the now-calibrated voltage on `/hv` directly - no probe needed for routine checks
9. (Optional) Set `Target` to your desired voltage to enable the closed-loop trim

If the displayed voltage drifts noticeably when you reconnect the tube, that's normal - tubes draw a few µA at threshold and most home-built boost stages have non-zero output impedance. Set the open-loop `Duty` to give your target voltage **with the tube connected**, then enable `Target` to track it through temperature and age.

## Generic HV Builds (esp8266_hv / esp32_hv)

1. Flash `esp8266_hv` or `esp32_hv` and wire your boost circuit (transistor, inductor, diode, cap, feedback divider). See [Build Targets](/install/buildtargets#hv-builds). Fit a pull-down on the transistor's gate or base: the pin floats until the firmware drives it low, early in boot.
2. HV starts **inactive**. On the [Config page](/configuration), System tab, **HV Hardware**, set **HV PWM pin** to the GPIO driving your transistor, save and **restart**.
3. Open `/hv` to set frequency, duty and, after calibration, the target.
4. Once calibrated, set a cut-off a little above your working voltage with `set espghw.ov_v <volts>`.

The feedback input is A0 on ESP8266 and GPIO 36 on ESP32.

## Closed-Loop Trim

With `Target` above 0, the firmware moves duty by 1 step every 10 seconds when the average of the last 8 readings is outside a band around the target, up to **±5 steps** from your saved `duty`. The band is 2 ADC counts either side, about ±2 V on ESPGeiger-HW. Set `duty` so HV is already right with the tube connected and `Target = 0`; the trim only tracks drift.

If the duty field shows `(trim +5)` or `(trim -5)`, the trim has hit its limit: move `duty` by hand in that direction until the trim sits near 0. The trim is kept over a restart. It resets on a power cut, an over-voltage cut-off, and any duty, frequency, target or ratio change.