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

CSV files are written to a subdirectory of the SD Card, using the following format:

`<year><month>/<year><month><day>.csv`

Files on the SDCard are written to every minute, giving the average values for the last minute.

A 4-8 GB card is recommended. The minutely rows use under 100 KB a day, so even a 2 GB card holds decades of them.

Each row is stamped with the time from NTP, from an [RTC chip](#time-without-a-network) if one is fitted, or on ESP32 kept over a restart (an ESP8266 loses it on a restart).

## Time without a network

Until the device knows the time, rows go to `undated.csv` in the card's root instead, with the seconds since boot in place of the date:

```
Uptime,CPM,uSv/h,CPM5,CPM15
1260,24.00,0.1600,23.80,22.10
synced,2026-11-04 10:15:30,1265
```

Each boot that writes there starts with the header line. When the time arrives a `synced` line gives the date and time at that many seconds since boot, so the rows above it can be dated: a row's time is the synced time minus the difference in seconds. From then on rows go to the daily files as usual. A boot that never gets the time leaves its rows with the seconds only. The SD page links the file as *Undated rows*.

An RTC chip on the I2C bus (DS3231, DS1307, PCF8563 or BM8563; ESP32-C3, ESP32-S2 and ESP32-S3 builds with the I2C bus, ESP8266 and classic ESP32 builds with `-DRTC_CHIP`) keeps the time over a power cut, so rows are dated from power-on. Without the time from either, press **Set from this browser** under *Clock not set* on the **Network Config** page (Status links to it). An RTC keeps that time too, so a device that never reaches NTP stays dated.

## (Micro) SD Card Compatibility

During testing most SDCards are compatible, however it seems some Micro SDCards simply do not support the SPI interface. If you find an SD Card which is not supported, please try another, after double checking your wiring.

The card must be formatted FAT32 (or FAT16). Cards over 32 GB (SDXC) come formatted exFAT, which isn't read: format them as FAT32 first.

The card is ready as soon as it mounts. Its free space is then counted in the background, a little at a time, so a larger card only takes longer to count. Until the count is done the SD page says *counting free space*. If the card can't be read during the count, it is tried again at the next minute and the page says *free space unknown* meanwhile.

## A full or failing card

Logging stops when the card is full with no file older than a year to clear, or when writes fail three minutes in a row. After failed writes it tries the card again by itself, 1, 2, 4 and 8 minutes later, then every 15 minutes. The SD Logs page says which, and the files already on the card can still be listed and downloaded. **Retry** on that page mounts the card again and starts logging if it can write; free some space or swap the card first.

# Configuration

The SD Card module exposes one setting on the `/param` configuration page:

<!-- gen:prefs sdcard -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Sync interval (min) | `sdcard.sync_min` | Int 1-5 | `1` | Minutes between syncs to the card. |

<!-- /gen -->

Each minute the firmware appends one CSV row. The file stays open between minutes to avoid the cost of re-opening on every write. Data is only physically committed to the card on sync - between syncs, rows sit in the firmware's RAM buffer.

- `1` (default) - sync on every write. Safest: at most the current in-progress row is lost on a power cut. Highest SD wear.
- `2`-`5` - sync every N minutes. Up to `N - 1` minutes of rows can be lost on a power cut, but SD write cycles are reduced proportionally. Useful on cheap / worn cards or battery-powered deployments where clean shutdown isn't guaranteed.

Regardless of the setting, the file is always flushed at day rollover (when a new daily CSV is opened) and before old files are cleared to make room (after the count at mount, and when the card looks full).

# CSV Format

```
DateTime,CPM,uSv/h,CPM5,CPM15
```

| Value | Description |  Example Value |
|---|---|---|
`DateTime` | Date and time of the row, in UTC. | `2023-12-23 01:23:45`
`CPM` | Current Counts Per Minute (CPM) value. | `30.00`
`uSv/h` | Current micro Sieverts per hour (µSv/h) value. | `0.1000`
`CPM5` | Average CPM over the last 5 minutes. | `30.00`
`CPM15` | Average CPM over the last 15 minutes. | `30.00`
