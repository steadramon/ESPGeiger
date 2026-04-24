---
layout: page
title: Serial Counters
permalink: /hardware/serial
parent: Hardware
nav_order: 3
---

# Serial Counters

ESPGeiger supports a variety of Geiger counters that communicate via serial interfaces. A single unified `serial` build supports all serial protocols — select your counter type from the **Config** page (System > Serial Type) after flashing.

## Supported Counters

| Type ID | Counter | Baud Rate | Serial Format | Notes |
|---|---|---|---|---|
| 1 | [NetIO GC10](https://www.ebay.co.uk/usr/pelorymate) | 9600 | `60\n` | [Forum](https://www.netiodev.com/GC10/jne.cgi?) |
| 2 | [NetIO GC10next](https://www.ebay.co.uk/usr/pelorymate) | 115200 | `60\n` | |
| 3 | [MightyOhm Kit](https://www.tindie.com/stores/mightyohm/) | 9600 | `CPS, ####, CPM, ####, uSv/hr, #.##, INST\n` | [Serial Info](https://mightyohm.com/blog/products/geiger-counter/usage-instructions/) |
| 4 | ESPGeiger | 115200 | `CPM: ####\n` | |

## Expected Reporting Frequency

All supported counters emit one reading per second. ESPGeiger treats the stream as unhealthy if no valid line has parsed for **60 seconds** — long enough to tolerate brief network / cable glitches while catching a genuinely disconnected or dead counter. The state is exposed via MQTT as a `binary_sensor` with `device_class: connectivity` named **Serial Connected** (JSON field `ser_ok`, values `1`/`0`), published into Home Assistant via autodiscovery when HA discovery is enabled. If your counter reports at a slower cadence (custom firmware etc.), the health flag will oscillate — at the moment the threshold is hard-coded, not configurable.

## How CPS, CPM5/CPM15 and Total Counts Are Derived

The firmware shares a single pipeline across pulse and serial builds — CPS, 5- and 15-minute averages, total counts, MQTT/API feeds, and alarm thresholds are all driven from a stream of per-second click events. How those events get produced depends on what the counter emits:

**CPM-only formats (GC10 / GC10Next / ESPGeiger-serial)** — the counter transmits just its rolling CPM value. ESPGeiger reverse-engineers synthetic per-second clicks from it:

```
synthetic_clicks_per_second = reported_CPM ÷ 60
```

These are accumulated in a fractional counter and pushed through the same 60-second rolling window used by the pulse firmware.

**CPS-bearing formats (MightyOhm)** — the counter transmits both CPS and CPM in every line, with the CPS field valid up to 65535 across all three of its internal modes (`SLOW` / `FAST` / `INST`). ESPGeiger uses that CPS value directly, so each second's count matches exactly what the counter measured regardless of which mode it's currently in.

Practical consequences:

- **Display lag (CPM-only formats).** The CPM on the ESPGeiger display is a smoothed view of an already-smoothed number. It will lag the counter's own display during a true rate change (e.g. waving a source past the tube). Instantaneous side-by-side readings will differ even at steady state — the counter's display is noisier, ESPGeiger's is quieter — but they describe the same underlying rate. The [CPM Window](#cpm-window-option) option shortens this lag.
- **Total counts (CPM-only formats).** ESPGeiger synthesises clicks from the reported CPM once per second; totals only include time ESPGeiger was running, not anything the counter counted before boot. In steady state both sides increment their totals at the same rate (within fractional rounding), so delta-over-time comparisons match; absolute totals will differ by whatever the counter had already counted before ESPGeiger started listening.
- **Total counts (CPS-bearing formats).** Exact per-second counts from ESPGeiger's boot onwards. MightyOhm's lifetime total still won't match if the counter was running before ESPGeiger booted, for the same reason.

## CPM Window Option

ESPGeiger averages received CPM values over a rolling window before displaying them, which keeps the reading steady at the cost of lagging the counter's own display whenever the true rate changes. The **CPM Window** setting (Config > Input > CPM Window, serial builds only) controls that window's length, from **1 to 60 seconds**.

Reference points:

- **60** — maximum smoothing. Reading is very steady, but lags up to a minute behind fast rate changes. Matches how the pulse-mode firmware behaves.
- **30** (default) — comfortable middle ground. Tracks rate changes in ~15 seconds and still smooths out most natural variance at background levels.
- **15** — more responsive. Tracks changes in under 10 seconds, slightly noisier at background.
- **5** — tracks source-wave demos within a few seconds, but natural Poisson jitter is clearly visible at background rates.
- **1** — effectively pass-through. You see whatever the counter just sent, second by second.

Things to know:

- Lower window values mean higher natural variance at steady state — CPM will bounce more, closer to what the counter's own display shows.
- Warning / alert thresholds can trip on short Poisson spikes that a 60-second window would have smoothed over. If you have tight thresholds, bump them up or use a larger window.
- Requires a reboot to apply (the window size is fixed at startup).

`total_clicks`, CPS, 5- and 15-minute averages, and all MQTT/API feeds continue to work unchanged at any window setting — only the 1-minute CPM's responsiveness is affected.

## Unsupported Counters

ESPGeiger may also function with additional serial Geiger counters that are not explicitly listed. These counters are considered theoretically compatible based on their communication protocols. However, due to limited resources, formal testing hasn't been possible.

Additional counters can be added easily within the codebase, if you own or can offer a device for testing and support, please get in touch.

## Other Serial Counters

At present the GQ Geiger Counter range of counters is unsupported due to the lack of access to a device. 

The information regarding the CQ protocol can be found here - https://www.gqelectronicsllc.com/download/GQ-RFC1201.txt

- GMC-300 Plus V4.xx
- GMC-320
