---
layout: page
title: Offline Mode
permalink: /configuration/offlinemode
nav_order: 5
parent: Configuration
---

# Offline Mode

Offline mode runs ESPGeiger as a standalone counter with Wi-Fi switched off. It suits the [ESPGeiger-HW](/hardware/espgeigerhw), [ESPGeiger-Log](/hardware/espgeigerlog) and [Cajoe GC-ESP32](/hardware/cajoeiot) devices, where the display shows the readings locally.

## Entering Offline Mode

Offline mode needs a build with a push button.

1. Power off or reset the device.
2. Press and hold the push button.
3. Power on or reset the device while holding the button.
4. **Release the button within <!-- gen:const CORIUM_BOOT_BUTTON_RESET_S s -->5 seconds<!-- /gen -->.** The display shows a countdown.

> Holding the button for <!-- gen:const CORIUM_BOOT_BUTTON_RESET_S s -->5 seconds<!-- /gen --> or more performs a **factory reset**: settings and Wi-Fi credentials are erased. The Stations identity and lifetime totals are kept. A hold of <!-- gen:const CORIUM_BOOT_BUTTON_STUCK_S s -->30 seconds<!-- /gen --> or more is taken as a stuck button and ignored: the device boots as normal, online.

Over the USB console, type `offline` while the device is in its setup portal or joining Wi-Fi: that boot goes offline the same way. `set wifi.mode 1` makes every boot offline until `set wifi.mode 0` (see [Serial](/output/serial)).

To keep the web UI without a network, `set wifi.mode 2` makes the device start its own hotspot instead (see [Serial](/output/serial#its-own-hotspot)).

## What Offline Mode Does

* Wi-Fi stays off. No uploads, no web portal, no network time.
* The Geiger counter, display and local outputs work as normal.
* SD card rows go to `undated.csv` with the seconds since boot, unless an RTC chip or a restart kept the time. See [SD Card](/output/sdcard#time-without-a-network).
* The display timeout is switched off.

To go back online, restart without holding the button.

Offline mode differs from a lost connection. If the saved Wi-Fi network is unavailable at boot, and the Wi-Fi details were only just changed, the device goes back to the previous network; if that fails too, that boot opens the [setup portal](/install/setup) on every build, with a **Last try** line that says why. If nobody uses it, the next boot tries again and opens it again, until a network joins. Otherwise the device keeps counting and starts its own hotspot for that boot, as `wifi.mode 2`: the network is the device's name (WiFi key = the web password if 8 or more characters, otherwise open) and the whole web UI is at 192.168.4.1; a phone or laptop that joins it is shown the Network page, so you can set a new network there. The hotspot goes after 10 minutes with nobody connected to it (`set wifi.hs_hold N` in the console to change it; the console says `WiFi: Hotspot off, nobody on it for 10 min`); then the device stays offline, still counting, retries the network every 2 minutes and restarts after an hour (`wifi.down_max`). When the saved network is back (a router still starting after a power cut), the next retry finds it and the device restarts to join: the console says `WiFi: Saved network back, restarting to reconnect`.
