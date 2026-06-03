---
layout: page
title: Pause Posts
permalink: /output/pause
parent: Outputs
nav_order: 90
---

# Pause External Posts

Temporarily stop posting to external services without changing prefs.
The pause auto-expires so you cannot forget about it.

Useful when calibrating against a known source, swapping a tube, or
working on the device.

## What gets paused

* [Radmon](/output/radmon)
* URADMonitor
* Safecast
* WebAPI (stations.espgeiger.com)
* [GMC / GeigerLog](/output/gmc)
* [Webhook](/output/webhook)
* [Thingspeak](/output/thingspeak)

## What stays running

* [MQTT](/output/mqtt) (your own broker / Home Assistant)
* [Web Portal](/output/webportal)
* [Serial](/output/serial)
* [OLED](/output/oled)
* [UDP local broadcast](/output/udp)

Counting, history, and lifetime totals also keep going.

## Console command

From the serial console (web or hardware):

| Command | Action |
|---|---|
| `pause 600` | Pause for 600 s |
| `pause 0` | Resume |
| `pause` | Print remaining time or `active` |

## HTTP

| URL | Action |
|---|---|
| `http://<device>/pause?s=600` | Pause for 600 s |
| `http://<device>/pause?s=0` | Resume |
| `http://<device>/pause` | Read state |

Returns `PAUSED <N> s` or `RESUMED`.

## Notes

* Max 24 h. Reboot clears any active pause.
* Set and auto-resume both log to console.
* One post fires on the next tick after the pause ends, then normal cadence.

## See also

* [CPM Mode](/configuration/cpm-mode)
* [Outputs index](/output)
