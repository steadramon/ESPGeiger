---
layout: page
title: Serial Counters
permalink: /hardware/serial
parent: Hardware
nav_order: 3
---

# Serial Counters

ESPGeiger supports a variety of Geiger counters that communicate via serial interfaces. A single unified `serial` build supports all serial protocols - select your counter type on the [Config page](/configuration) under **Input > Serial Type** after flashing.

## Supported Counters

<!-- gen:stypes -->

| Type ID | Counter | Baud Rate | Serial Format | Notes |
|---|---|---|---|---|
| 1 | [NetIO GC10](https://www.ebay.co.uk/usr/pelorymate) | 9600 | `60\n` | [Forum](https://www.netiodev.com/GC10/jne.cgi?) |
| 2 | [NetIO GC10next](https://www.ebay.co.uk/usr/pelorymate) | 115200 | `60\n` | |
| 3 | [MightyOhm Kit](https://www.tindie.com/stores/mightyohm/) | 9600 | `CPS, ####, CPM, ####, uSv/hr, #.##, INST\n` | [Serial Info](https://mightyohm.com/blog/products/geiger-counter/usage-instructions/) |
| 4 | ESPGeiger | 115200 | `CPM: ####\n` | |
| 5 | Template | 115200 | Any line: the number after `CPM`, else the first number | Pairs with another ESPGeiger's [user template](/output/serial#user-template-format-2) output. `CPS` is read when present. |

<!-- /gen -->

## Expected Reporting Frequency

All supported counters send one reading per second. If no valid line arrives for **60 seconds**, ESPGeiger marks the counter as disconnected. This shows in MQTT and Home Assistant as **Serial Connected** (JSON field `ser_ok`). Counters that report less often than once a minute will flip this flag.

## How readings are derived

- **CPM-only counters (GC10, GC10Next, ESPGeiger).** ESPGeiger turns each reported CPM into counts per second (CPM ÷ 60). Its display is a smoothed view of the counter's own smoothed number, so it lags the counter's display when the rate changes. The [CPM Window](#cpm-window-option) option shortens the lag.
- **MightyOhm.** The counter sends its exact counts per second, and ESPGeiger uses them directly.
- **Totals** count only from when ESPGeiger started listening, so they will not match the counter's own lifetime total.

## CPM Window Option

ESPGeiger averages received CPM values over a rolling window before displaying them, which keeps the reading steady at the cost of lagging the counter's own display whenever the true rate changes. The **CPM Window (CPM-only counters)** setting (Config > Input, serial builds only) controls that window's length, from **1 to 60 seconds**.

It only applies to CPM-only counters (GC10, GC10Next, ESPGeiger). MightyOhm always uses 60 seconds, because it sends exact per-second counts.

Reference points:

- **60** - maximum smoothing. Reading is very steady, but lags up to a minute behind fast rate changes. Matches how the pulse-mode firmware behaves.
- **30** (default) - comfortable middle ground. Tracks rate changes in ~15 seconds and still smooths out most natural variance at background levels.
- **15** - more responsive. Tracks changes in under 10 seconds, slightly noisier at background.
- **5** - tracks source-wave demos within a few seconds, but natural Poisson jitter is clearly visible at background rates.
- **1** - effectively pass-through. You see whatever the counter just sent, second by second.

Things to know:

- Lower window values mean higher natural variance at steady state - CPM will bounce more, closer to what the counter's own display shows.
- Warning / alert thresholds can trip on short Poisson spikes that a 60-second window would have smoothed over. If you have tight thresholds, bump them up or use a larger window.
- Requires a reboot to apply (the window size is fixed at startup).

`total_clicks`, CPS, 5- and 15-minute averages, and all MQTT/API feeds continue to work unchanged at any window setting - only the 1-minute CPM's responsiveness is affected.

## Unsupported Counters

ESPGeiger may also function with additional serial Geiger counters that are not explicitly listed. These counters are considered theoretically compatible based on their communication protocols. However, due to limited resources, formal testing hasn't been possible.

Additional counters can be added easily within the codebase, if you own or can offer a device for testing and support, please get in touch.

## Other Serial Counters

At present the GQ Geiger Counter range of counters is unsupported due to the lack of access to a device. 

The information regarding the CQ protocol can be found here - https://www.gqelectronicsllc.com/download/GQ-RFC1201.txt

- GMC-300 Plus V4.xx
- GMC-320
