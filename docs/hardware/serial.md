---
layout: page
title: Serial Counters
permalink: /hardware/serial
parent: Hardware
nav_order: 3
---

# Serial Counters

ESPGeiger supports a variety of Geiger counters that communicate via serial interfaces. However, comprehensive testing has only been conducted with models for which hardware access was available.

## Supported Counters

A list of currently supported serial Geiger counters can be found below

| Counter | Image | Serial Rate | Serial Example | Link |
|---|---|---|---|---|
MightyOhm Kit | ![MightyOhm](img/mightyohm.jpg#img-thumbnail) | 9600 | `CPS, ####, CPM, ####, uSv/hr, #.##, [INST/FAST/SLOW]\n` | https://www.tindie.com/stores/mightyohm/
NetIO GC10 | ![NetIO GC10](img/gc10.jpg#img-thumbnail) | 9600 | `60\n` | https://www.ebay.co.uk/usr/pelorymate
NetIO GC10next | ![NetIO GC10next](img/gc10next.jpg#img-thumbnail) | 115200 | `60\n` | https://www.ebay.co.uk/usr/pelorymate

## Unsupported Counters

ESPGeiger may also function with additional serial Geiger counters that are not explicitly listed. These counters are considered theoretically compatible based on their communication protocols. However, due to limited resources, formal testing hasn't been possible.

Additional counters can be added easily within the codebase, if you own or can offer a device for testing and support, please get in touch.

## Other Serial Counters

At present the GQ Geiger Counter range of counters is unsupported due to the lack of access to a device. 

The information regarding the CQ protocol can be found here - https://www.gqelectronicsllc.com/download/GQ-RFC1201.txt

- GMC-300 Plus V4.xx
- GMC-320