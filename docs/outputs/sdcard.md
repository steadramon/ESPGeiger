---
layout: page
title: SDCard
permalink: /output/sdcard
parent: Outputs
nav_order: 10
---

# SD Output

Where the hardware is supported, ESPGeiger is able to output information to a SD Card attached via SPI to the ESP MCU.

Please note at this time only the [ESPGeiger Log](/hardware/espgeigerlog) hardware is supported, a Micro SD Card slot is included on the hardware board.

Output to CSV files are written to a subdirectory of the SDCard, following the following format:

`<year><month>/<year><month><day>.csv`

Files on the SDCard are written to every minute, giving the average values for the last minute.

A SD Card of 4-8GB is recommended - an SD Card of 2GB should give enough capacity for ~20 years of minutely CSV information.

## (Micro) SD Card Compatibility

During testing most SDCards are compatible, however it seems some Micro SDCards simply do not support the SPI interface. If you find an SD Card which is not supported, please try another, after double checking your wiring.

During initialisation of ESPGeiger the size and free space of the SD Card is calculated - the larger the SD Card, the longer this takes.

A SD Card of up to 16GB usually takes < 2 seconds to initialise.

# CSV Format

```
Unixtime, CPM, μSv/h, CPM5, CPM15
```

| Value | Description |  Example Value |
|---|---|---|
`Datetime` | Current datetime. | `30.0`
`CPM` | Current Counts Per Minute (CPM) value. | `30.0`
`uSv` | Current micro Sieverts per hour (μSv) value. | `0.10`
`CPM5` | Average CPM over the last 5 minutes. | `30.0`
`CPM15` | Average CPM over the last 15 minutes. | `30.0`
