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

Until the clock is set:

- [SD card](/output/sdcard) rows go to `undated.csv` with the seconds since boot.
- Quiet hours for the blip LED are ignored.
- The display On time / Off time schedule is ignored and the display stays on.
