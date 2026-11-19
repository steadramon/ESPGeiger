---
layout: page
title: NTP
permalink: /configuration/ntp
nav_order: 10
parent: Configuration
---

# Time and NTP

ESPGeiger sets its clock from the internet using NTP. Set the time zone and NTP server on the **Network Config** page (`/network`): expand **Time / NTP**.

- **Timezone** - the zone used for times shown on the device. Default `Etc/UTC`.
- **NTP server** - the server to sync with. An IP address or host name. Default `pool.ntp.org`.

Only canonical time zone names are listed. If yours is missing, pick a zone it links to. See the [list of tz database time zones](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones).

Not listed: `set ntp.tz` also takes a POSIX rule, e.g. `EST5EDT,M3.2.0,M11.1.0`. The form refuses a name it doesn't know; from the console an unknown name runs as UTC and the console says so.

## Without NTP

Offline, or with no way to the internet, the clock can come from:

- An RTC chip on the I2C bus: DS3231, DS1307, PCF8563 or BM8563. C3, S2 and S3 builds look for one at boot; ESP8266 and ESP32 builds need `-DRTC_CHIP`. It keeps the time over a power cut and is set from NTP or the browser. The boot log says `RTC: DS3231 found` (or `PCF8563`).
- **Set from this browser** under *Clock not set* on the **Network Config** page.
- On ESP32 a restart keeps the time; an ESP8266 loses it.

Until the clock is set:

- [SD card](/output/sdcard) rows go to `undated.csv` with the seconds since boot.
- Quiet hours for the blip LED are ignored.
- The display On time / Off time schedule is ignored and the display stays on.
