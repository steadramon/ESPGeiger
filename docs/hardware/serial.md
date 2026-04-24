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

- **Display lag (CPM-only formats).** The CPM on the ESPGeiger display is a smoothed view of an already-smoothed number. It will lag the counter's own display by up to ~60 seconds during a true rate change (e.g. waving a source past the tube). Instantaneous side-by-side readings will differ even at steady state — the counter's display is noisier, ESPGeiger's is quieter — but they describe the same underlying rate.
- **Total counts (CPM-only formats).** Over an hour of steady operation the total agrees with the counter's true total within about **1%**; over 24 hours within **~0.03%**. The small gap comes almost entirely from the first minute after boot while the rolling window fills from zero.
- **Total counts (CPS-bearing formats).** Exact, modulo only the single boot second during which the serial stream hasn't started yet. MightyOhm totals should match to within a count or two.

## Unsupported Counters

ESPGeiger may also function with additional serial Geiger counters that are not explicitly listed. These counters are considered theoretically compatible based on their communication protocols. However, due to limited resources, formal testing hasn't been possible.

Additional counters can be added easily within the codebase, if you own or can offer a device for testing and support, please get in touch.

## Other Serial Counters

At present the GQ Geiger Counter range of counters is unsupported due to the lack of access to a device. 

The information regarding the CQ protocol can be found here - https://www.gqelectronicsllc.com/download/GQ-RFC1201.txt

- GMC-300 Plus V4.xx
- GMC-320
