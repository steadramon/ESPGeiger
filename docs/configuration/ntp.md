---
layout: page
title: NTP
permalink: /configuration/ntp
nav_order: 10
parent: Configuration
---

# Time and NTP

ESPGeiger sets its clock from the internet using NTP. Set the time zone and NTP server on the **Network Config** page (`/network`): expand **Time / NTP**.

- **Timezone** - the zone used for times shown on the device.
- **NTP Server** - the server to sync with. An IP address or host name. Default `pool.ntp.org`.

Only canonical time zone names are listed. If yours is missing, pick a zone it links to. See the [list of tz database time zones](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones).

Until the clock is set:

- [SD card](/output/sdcard) logging pauses.
- Quiet hours for the blip LED are ignored.
- The display On Time / Off Time schedule is ignored and the display stays on.
