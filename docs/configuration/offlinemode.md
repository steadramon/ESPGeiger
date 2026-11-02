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

Over the USB console, type `offline` while the device is in its setup portal or joining Wi-Fi: that boot goes offline the same way. `set wifi.mode 1` makes every boot offline until `set wifi.mode 0` (see [Serial](/outputs/serial)).

To keep the web UI without a network, `set wifi.mode 2` makes the device start its own hotspot instead (see [Serial](/outputs/serial#its-own-hotspot)).

## What Offline Mode Does

* Wi-Fi stays off. No uploads, no web portal, no network time.
* The Geiger counter, display and local outputs work as normal.
* SD card rows go to `undated.csv` with the seconds since boot, unless an RTC chip or a restart kept the time. See [SD Card](/outputs/sdcard#time-without-a-network).
* The display timeout is switched off.

To go back online, reboot without holding the button.

Offline mode differs from a lost connection. If the saved Wi-Fi network is unavailable at boot, and the Wi-Fi details were only just changed, the device goes back to the previous network. Otherwise ESPGeiger-HW and ESPGeiger Lite/Log devices keep counting offline, retry the network every 2 minutes and reboot after an hour. Other builds open the [setup portal](/install/setup) hotspot, and reboot to try the saved network again after 10 minutes with nobody connected to the hotspot, or an hour after boot.
