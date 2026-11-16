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
4. **Release the button within 5 seconds.** The display shows a countdown.

> Holding the button for 5 seconds or more performs a **factory reset**: settings and Wi-Fi credentials are erased. The Stations identity and lifetime totals are kept. A hold of 30 seconds or more is taken as a stuck button and ignored: the device boots as normal, online.

Over the USB console, type `offline` while the device is in its setup portal or joining Wi-Fi: that boot goes offline the same way. `set wifi.mode 1` makes every boot offline until `set wifi.mode 0` (see [Serial](/output/serial)).

To keep the web UI without a network, `set wifi.mode 2` makes the device start its own hotspot instead (see [Serial](/output/serial#its-own-hotspot)).

## What Offline Mode Does

* Wi-Fi stays off. No uploads, no web portal, no network time.
* The Geiger counter, display and local outputs work as normal.
* SD card rows go to `undated.csv` with the seconds since boot, unless an RTC chip or a restart kept the time. See [SD Card](/output/sdcard#time-without-a-network).
* The display timeout is switched off.

To go back online, restart without holding the button.

Offline mode differs from a lost connection. If the saved Wi-Fi network is unavailable at boot, and the Wi-Fi details were only just changed, the device goes back to the previous network; if that fails too, that boot opens the [setup portal](/install/setup) on every build, with a **Last try** line that says why. If nobody uses it, the next boot tries again and opens it again, until a network joins. Otherwise ESPGeiger-HW and ESPGeiger Lite/Log devices keep counting and start their own hotspot for that boot, as `wifi.mode 2`: the network is the device's name (WiFi key = the web password if 8 or more characters, otherwise open) and the whole web UI is at 192.168.4.1, so you can set a new network on the Network page. The hotspot goes after 10 minutes with nobody connected to it (the console says `WiFi: Hotspot off, nobody on it for 10 min`); then the device stays offline, still counting, retries the network every 2 minutes and restarts after an hour. Other builds open the [setup portal](/install/setup), and restart to try the saved network again after 10 minutes with nobody connected to the setup portal, or an hour after boot. If the saved network was not there at all when the portal opened (a router still starting after a power cut), the device looks for it once a minute while nobody is connected to the setup portal, and restarts to join as soon as it shows up: the console says `WiFi: <network> back, restarting to join`.
