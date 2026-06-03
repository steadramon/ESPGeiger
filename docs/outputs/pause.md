---
layout: page
title: Pause Posts
permalink: /output/pause
parent: Outputs
nav_order: 90
---

# Pause External Posts

Temporarily suppress posts to public fleet aggregators and user-configured
endpoints without disabling them in prefs. The pause auto-expires after a
duration you set, so a forgotten pause cannot leave a device silently
offline forever.

Typical reasons to pause:

* Calibration test against a known source - keep spike readings out of
  the public time series.
* Tube swap or hardware change while the device stays powered.
* Local fault-finding where you do not want junk values polluting Radmon
  or Home Assistant history.
* Network maintenance on the upstream endpoint.

## What gets paused

The seven modules that ship measurements off the device:

* [Radmon](/output/radmon)
* URADMonitor
* Safecast
* WebAPI (stations.espgeiger.com)
* [GMC / GeigerLog](/output/gmc) protocol
* [Webhook](/output/webhook)
* [Thingspeak](/output/thingspeak)

Each one's scheduler keeps running; the actual upload is skipped while
paused.

## What does NOT get paused

The local UI keeps working as normal so you can monitor what is happening:

* [MQTT](/output/mqtt) - stays live so your Home Assistant / broker
  shows current state and does not mark entities unavailable
* [Web Portal](/output/webportal) (`/status`, `/json`, `/lastdata`,
  `/clicks`)
* [Serial output](/output/serial) (live serial / GeigerLog connection)
* [OLED display](/output/oled)
* [UDP local broadcast](/output/udp) (peer-to-peer between your own
  ESPGeiger devices)

Counting, history, lifetime totals, and the inter-pulse histogram all
keep updating during a pause - only the upstream posts stop.

## Using it - serial / console command

From the **Web Portal serial console** (the box on `/status`), the
**Test Mode console** (`/console`), or a real serial connection:

```
pause 600
```

Pauses external posts for 600 seconds (10 minutes). Output:

```
External posts: paused for 600 s
```

Resume early:

```
pause 0
```

Check current state:

```
pause
```

Prints either `External posts: paused 543 s remaining` or
`External posts: active`.

## Using it - HTTP / browser

Hit `/pause` on the device:

| URL | Action |
|---|---|
| `http://<device>/pause?s=600` | Pause for 600 s |
| `http://<device>/pause?s=3600` | Pause for 1 hour |
| `http://<device>/pause?s=0` | Resume immediately |
| `http://<device>/pause` | Read current state |

The endpoint returns `PAUSED <N> s` or `RESUMED` as plain text - the
browser tab will show the answer directly.

## Safety properties

* **Auto-expires.** The pause window is checked on each module's next
  scheduled tick. Once the deadline passes, the state clears and posts
  resume automatically. You cannot leave a device permanently silent by
  forgetting.
* **24 hour hard cap.** Any value above 86400 (24 h) is clamped. If you
  need longer than 24 h, refresh the pause periodically or use the
  module's pref-level `enabled` toggle.
* **Reboot clears it.** Pause state lives in RAM only - a power cycle
  resumes posts immediately. No persistent state to forget.
* **Logged.** `External posts: paused for N s` on set and
  `External posts: resumed (timeout)` on auto-expire show up in the
  console log so you can see what happened later.

## What happens when the pause ends

Each module's scheduler did not advance its `lastPing` timestamp while
paused. When the pause expires, the next tick sees that the schedule is
overdue and fires one post immediately, then resumes normal cadence -
no flood, no scheduling drift.

If you want to suppress that catch-up post too, resume manually with
`pause 0` followed by waiting for the next natural tick. In practice the
single catch-up post is exactly what you usually want.

## Cross references

* [CPM Mode](/configuration/cpm-mode) - how the active calculation mode
  affects what each output module would post.
* [Outputs index](/output) - per-module configuration.
