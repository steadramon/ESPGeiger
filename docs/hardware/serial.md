---
layout: page
title: Serial Counters
permalink: /hardware/serial
parent: Hardware
nav_order: 2
---

# Serial Counters

The ESPGeiger firmware is compatible with the following Serial based counters. Compatibility has only been fully tested where access to the hardware has been available.
A number of Serial based counters that are supported are untested but theoretically fine. If you own or can offer a device below for testing and support, please get in touch.
Additional counters are a

| Counter | Image | Serial Rate | Serial Example | Link |
|---|---|---|---|---|
MightyOhm Kit | ![MightyOhm](img/mightyohm.jpg#img-thumbnail) | 9600 | `CPS, ####, CPM, ####, uSv/hr, #.##, [INST/FAST/SLOW]\n` | https://www.tindie.com/stores/mightyohm/
NetIO GC10 | ![NetIO GC10](img/gc10.jpg#img-thumbnail) | 9600 | `60\n` | https://www.ebay.co.uk/usr/pelorymate
NetIO GC10next | ![NetIO GC10next](img/gc10next.jpg#img-thumbnail) | 115200 | `60\n` | https://www.ebay.co.uk/usr/pelorymate

## Other Serial Counters

At present the GQ Geiger Counter range of counters is unsupported due to the lack of access to a device. 

The information regarding the CQ protocol can be found here - https://www.gqelectronicsllc.com/download/GQ-RFC1201.txt

- GMC-300 Plus V4.xx
- GMC-320