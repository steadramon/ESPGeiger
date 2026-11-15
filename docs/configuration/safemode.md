---
layout: page
title: Safe Mode
permalink: /configuration/safemode
nav_order: 6
parent: Configuration
---

# Safe Mode

Safe mode starts ESPGeiger with only the core parts running: WiFi, the web pages, settings, firmware update and the serial console. It is there so a device that keeps crashing can still be reached and fixed. On HV builds the HV pin is held low, so the high voltage stays off.

## When it starts

* **After 3 crashes in a row**, each before the device has run for 10 minutes. A crash here is an exception, a watchdog reset or a brownout, so a poor power supply can trigger it too (see [Troubleshooting](/troubleshooting)).
* **On request**, with the `safe` command over [serial](/output/serial), or by opening `/guard?enter=1`. The device restarts into safe mode.

Installing a new firmware clears the crash count.

## What it does

* Counting stops. No uploads, MQTT, display, sounds or other outputs.
* HV stays off on HV builds.
* The home page shows **SAFE MODE**, why it started, and how long until it tries normal mode again. An OLED shows *Safe mode* when it first starts.
* The **Config**, **Network Config**, **Info** and **Update** pages still work. Config shows every setting, so a recent change can be undone. On ESPGeiger-HW and the `_hv` builds the HV settings are the exception: `/hv` is off in safe mode, so undo them with `set espghw.duty <n>` or `reset espghw` over serial.
* `/info` shows the latest crash, worth including in a bug report.
* The Status and History pages aren't there, nor is the command box on the Status page or `/json`. Use the [serial console](/output/serial) for commands; it works as normal, except on ESPGeiger Log and ESPGeiger Lite builds, which read no serial commands.
* `/outputs` lists each stopped module as `{"quarantined":true}`.
* A short hold of the push button at boot does nothing, so no offline mode. A hold of 5 seconds or more still does a factory reset.

## Getting out

* Wait. The device restarts and tries normal mode after 30 minutes. If it ends up back in safe mode the wait doubles each time, up to 16 hours, and goes back to 30 minutes once the device has run for a day.
* Restart it: the **restart now** link on the home page, the `reboot` command, or a power cycle. The next boot is normal.
* Install a different firmware from the **Update** page.

If the crashes come back, it returns to safe mode after another 3.
