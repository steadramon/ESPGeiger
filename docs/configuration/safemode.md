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

* **After <!-- gen:const CORIUM_BOOTGUARD_CRASHES -->3<!-- /gen --> crashes in a row**, each before the device has run for <!-- gen:const CORIUM_BOOTGUARD_LIVED_S s -->10 minutes<!-- /gen -->. A crash here is an exception or a watchdog reset. A brownout, from a poor power supply, doesn't count (see [Troubleshooting](/troubleshooting)).
* **On request**, with the `safe` command over [serial](/output/serial), or by opening `/guard?enter=1`. The device restarts into safe mode.

Installing a new firmware clears the crash count.

## What it does

* Counting stops. No uploads, MQTT, display, sounds or other outputs.
* HV stays off on HV builds.
* The home page shows **SAFE MODE**, why it started, and how long until it tries normal mode again. An OLED shows *Safe mode* when it first starts.
* The **Config**, **Network Config**, **Info** and **Update** pages still work. Config shows every setting, so a recent change can be undone. On ESPGeiger-HW and the `_hv` builds the HV settings are the exception: `/hv` is off in safe mode, so undo them with `set espghw.duty <n>` or `reset espghw` over serial.
* `/info` shows the latest crash, worth including in a bug report.
* The Status and History pages aren't there, nor is the command box on the Status page or `/json`. Use the [serial console](/output/serial): `set`, `get`, `reset`, `reboot` and the other built-in commands work, the rest answer `off in safe mode`. ESPGeiger Log builds read no serial commands.
* `/outputs` lists each stopped module as `{"quarantined":true}`.
* A short hold of the push button at boot does nothing, so no offline mode. A hold of <!-- gen:const CORIUM_BOOT_BUTTON_RESET_S s -->5 seconds<!-- /gen --> or more still does a factory reset.

## Getting out

* Wait. The device restarts and tries normal mode after <!-- gen:const CORIUM_BOOTGUARD_RETRY_MS ms -->30 minutes<!-- /gen -->. If it ends up back in safe mode the wait doubles each time, up to <!-- gen:const CORIUM_BOOTGUARD_RETRY_MS<<RETRY_DOUBLINGS ms -->16 hours<!-- /gen -->, and goes back to <!-- gen:const CORIUM_BOOTGUARD_RETRY_MS ms -->30 minutes<!-- /gen --> once the device has run for a day.
* Restart it: the **restart now** link on the home page, the `reboot` command, or a power cycle. The next boot is normal. The device's own restarts, for a lost WiFi network or a setup portal that timed out, stay in safe mode.
* Install a different firmware from the **Update** page. On HV builds, tick **Another build (an older firmware)** to install another build's bin.

After safe mode, one crash in the first <!-- gen:const CORIUM_BOOTGUARD_LIVED_S s -->10 minutes<!-- /gen --> takes it straight back.
