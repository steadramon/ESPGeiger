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

NTP synchronisation is required to write to the SD Card. Writing to the file takes place once per minute.

## (Micro) SD Card Compatibility

During testing most SDCards are compatible, however it seems some Micro SDCards simply do not support the SPI interface. If you find an SD Card which is not supported, please try another, after double checking your wiring.

During initialisation of ESPGeiger the size and free space of the SD Card is calculated - the larger the SD Card, the longer this takes.

A SD Card of up to 16GB usually takes < 2 seconds to initialise.

# Configuration

The SD Card module exposes one setting on the `/param` configuration page:

| Setting | Default | Range | Description |
|---|---|---|---|
| `Sync Interval (min)` | `1` | `1`-`5` | Minutes between syncs to the card. |

Each minute the firmware appends one CSV row. The file stays open between minutes to avoid the cost of re-opening on every write. Data is only physically committed to the card on sync — between syncs, rows sit in the firmware's RAM buffer.

- `1` (default) — sync on every write. Safest: at most the current in-progress row is lost on a power cut. Highest SD wear.
- `2`-`5` — sync every N minutes. Up to `N - 1` minutes of rows can be lost on a power cut, but SD write cycles are reduced proportionally. Useful on cheap / worn cards or battery-powered deployments where clean shutdown isn't guaranteed.

Regardless of the setting, the file is always flushed at day rollover (when a new daily CSV is opened) and before the daily cleanup scan.

# CSV Format

```
Datetime, CPM, uSv/h, CPM5, CPM15
```

| Value | Description |  Example Value |
|---|---|---|
`Datetime` | Current datetime. | `2023-12-23T01:23:45`
`CPM` | Current Counts Per Minute (CPM) value. | `30.0`
`uSv` | Current micro Sieverts per hour (μSv) value. | `0.10`
`CPM5` | Average CPM over the last 5 minutes. | `30.0`
`CPM15` | Average CPM over the last 15 minutes. | `30.0`
