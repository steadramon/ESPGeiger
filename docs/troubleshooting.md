---
layout: page
title: Troubleshooting
permalink: /troubleshooting
nav_order: 7
---

# Troubleshooting

Common problems, what they look like, and where to look first.

## Device restarts by itself / "Last reset: Brownout"

**Sort out your power supply.** This is by far the most common hardware
problem, and almost never a firmware issue.

On ESP32 boards the `/info` page reports it directly: **Last reset:
Brownout** means the supply voltage dipped below the safe threshold and the
chip protected itself by resetting. The usual suspects, in order:

- **Weak USB supply or cable.** Phone chargers and long/thin USB leads sag
  under load. WiFi transmit bursts draw sharp current spikes; a marginal
  supply drops just long enough to trigger a reset. Use a supply rated 1A+
  and a short, decent cable.
- **Sharing the supply with the geiger counter.** ESP32 devices draw a lot of power
- **Powering from a PC hub port.** Unpowered hubs are a classic source of
  mystery resets.

ESP8266 boards have no brownout detector, so an undervoltage problem shows
up differently: random resets ("Hardware Watchdog"), WiFi dropouts, or
crashes that never repeat in the same place. If a device misbehaves
erratically and the crash reasons keep changing, try a better power supply
before anything else.

## Device is online but shows 0 CPM

The firmware warns about this on the Status page ("No counts detected -
check tube and input wiring", with the input GPIO), in the console log, and
on the OLED display. Zero is never a valid reading - normal background is
roughly 10-40 CPM for common tubes - so a silent counter means the pulses
are not reaching the board:

- **Tube seating.** Reseat the tube in its clips; check for corrosion.
- **Pulse wire on the wrong pin.** The warning names the GPIO the firmware
  is listening on - confirm the board wiring actually goes there.
- **High voltage not present.** No HV, no pulses. On boards with HV
  feedback, check the HV reading on the Status page; otherwise carefully
  verify the supply with a suitable meter and probe resistor.
- **Pulse polarity or level.** Counter boards differ in whether the pulse
  output idles high or low. If everything is wired and powered but silent,
  check the counting board's output against what your build expects.

The warning clears itself the moment the first count arrives.

## WiFi keeps dropping

- Check **RSSI** on the `/info` page: weaker than about -80 dBm is
  unreliable in practice. Move the device or the access point.
- A reduced **TX power** setting saves energy but shrinks range - the
  current value is shown on `/info`.
- Erratic drops combined with random resets: see the power supply section
  above - WiFi transmit is exactly when a weak supply gives out.

## Readings look wrong

- **uSv/h is off but CPM looks right**: check the tube ratio setting -
  dose rate is derived from CPM through the ratio, and the default only
  suits common tube types.
- **CPM much higher than expected**: look for electrical noise pickup -
  long unshielded pulse wires near the HV side can double-count.

## When reporting a problem

Include the `/info` page contents (platform, version, last reset reason)
and, for crashes, the exception details shown there. A photo of the wiring
helps more often than you would expect.
