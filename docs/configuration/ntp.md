---
layout: page
title: NTP
permalink: /configuration/ntp
nav_order: 10
parent: Configuration
---

# NTP Configuration

ESPGeiger makes use of Network Time Protocol to keep time within the firmware. NTP is automatically kept in synch using the built-in system functions of the ESP firmware.

NTP synchronisation is required for a number of functions within the firmware, including:

- Accurate SD Card writing

## Options

- Timezone - The timezone for locally shown times.
- NTP Server - The server to contact for NTP time, accepts IP address or domain name.

Please note that only the Canonical timezones are supported - in case your specific zone is not listed, a linked timezone can be used. See [https://en.wikipedia.org/wiki/List_of_tz_database_time_zones](https://en.wikipedia.org/wiki/List_of_tz_database_time_zones) for more information.