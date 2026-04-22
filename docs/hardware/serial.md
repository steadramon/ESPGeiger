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

## Unsupported Counters

ESPGeiger may also function with additional serial Geiger counters that are not explicitly listed. These counters are considered theoretically compatible based on their communication protocols. However, due to limited resources, formal testing hasn't been possible.

Additional counters can be added easily within the codebase, if you own or can offer a device for testing and support, please get in touch.

## Other Serial Counters

At present the GQ Geiger Counter range of counters is unsupported due to the lack of access to a device. 

The information regarding the CQ protocol can be found here - https://www.gqelectronicsllc.com/download/GQ-RFC1201.txt

- GMC-300 Plus V4.xx
- GMC-320
