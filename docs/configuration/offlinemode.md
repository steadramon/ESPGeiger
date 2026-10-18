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

> Holding the button for 5 seconds or more performs a **factory reset**: settings and Wi-Fi credentials are erased.

## What Offline Mode Does

* Wi-Fi stays off. No uploads, no web portal, no network time.
* The Geiger counter, display and local outputs work as normal.
* SD card logging pauses, because it needs network time to stamp each row.
* The display timeout is switched off.

To go back online, reboot without holding the button.

Offline mode differs from a lost connection. If the saved Wi-Fi network is unavailable at boot, ESPGeiger-HW and ESPGeiger Lite/Log devices keep counting offline, retry the network every 2 minutes and reboot after an hour. Other builds reboot and try again.
