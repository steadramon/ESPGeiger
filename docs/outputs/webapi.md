---
layout: page
title: ESPGeiger Stations
permalink: /output/webapi
parent: Outputs
nav_order: 20
published: true
---

# ESPGeiger Stations - Public WebAPI

ESPGeiger can optionally contribute its readings to the public map at
[stations.espgeiger.com](https://stations.espgeiger.com), a global
radiation-monitoring grid built from community-run Geiger counters.
Participation is opt-in, signed end-to-end and privacy-respecting.

## What is shared

- **Every minute** (CPM Readings mode only): CPM and µSv/h, plus HV,
  temperature, humidity and pressure where the hardware has them.
- **Every 15 minutes**: a health snapshot of uptime, free memory, loop
  rate and Wi-Fi signal. Kept short-term only, never added to history.
- **Every hour**: a handshake with the device's public key, chip ID,
  firmware version, board and build, tube model and type, compiled-in
  features, last reset reason, sharing mode and, if you set one, your
  location rounded to about 1 km.

Field-level details are in the [WebAPI protocol](/dev/webapi-protocol) developer page.

## Configuring on the device

The `/webapi` page on each device shows the ESPGeiger Network controls:

![ESPGeiger Network config page](/img/espgeiger-network.png)

- **Station ID** - assigned by the server on first handshake. Click to view your station on `stations.espgeiger.com`.
- **Sharing** - tri-state: Off / Heartbeat / CPM Readings (see below).
- **Latitude / Longitude** - optional. Leave both at `0` to fall back to coarse IP-based geolocation. Coordinates are rounded to 2 decimal places (~1 km resolution) before publication.
- **Find location** - uses browser geolocation to fill in the lat/lon fields.
- **Advanced** - `Reset key` (rotate identity, register as a new station) and `Forget station` (server-side delete; see below).

## Sharing modes

A single tri-state pref (`webapi.mode`, default `2`) controls what
leaves the device:

| Mode | Label | Behaviour |
|---|---|---|
| `2` | **CPM Readings** *(default)* | Hourly handshake, per-minute readings, 15-min health snapshots. Your station appears on the map. |
| `1` | **Heartbeat**                | Hourly handshake + 15-min health snapshots only. No radiation data leaves the device. Your station appears in fleet-size / uptime stats but not on the map. |
| `0` | **Off**                      | Nothing sent - no handshake, no posts. |

Switching modes is a single click on the device's `/webapi` page. The
posting cadence changes immediately; the server learns the new mode at
the next handshake, within an hour.

## Identity and signing

Each device generates an ECC keypair on first run; every post is signed
with it. The server pins your public key on first handshake and refuses
unsigned or wrong-key posts. No shared secrets, no passwords to leak,
and your station can't be impersonated.

The `/webapi` page's **Advanced** panel exposes two destructive actions:

- **Reset key** wipes the local private key and reboots. The next
  handshake registers a fresh station; the old one ages out server-side.
  Use this to migrate to a new identity while still contributing data.
- **Forget station** sends a signed delete request. The server
  purges the row, all readings and all history, then sharing flips to
  **Off**. Irreversible - re-enabling later registers as brand new. This
  is the GDPR right-to-erasure path.

## Map presence

Once you've submitted a handshake, visit
`https://stations.espgeiger.com/station/<id>` to see your station. Each
station also gets a memorable three-word alias - e.g.
`curie-counts-photons` - which you can share directly:
`https://stations.espgeiger.com/station/curie-counts-photons`.

## Pin colours and events

Each station is a teardrop pin on the map. Colour reflects the station's
current reading vs **its own baseline**, not a global threshold. A tube
with high local background (e.g. sitting next to granite) is only flagged
when it departs from its own typical rate.

| Colour | State | Meaning |
|---|---|---|
| Blue       | New      | Fewer than ~4 h of data. Baseline not yet established. |
| Green      | Normal   | Reading consistent with baseline. |
| Yellow     | Elevated | Mild departure above baseline. Often routine variation. |
| Orange     | Warning  | Larger or longer-lasting departure. Common for weather events (radon washout after rain). |
| Red        | Danger   | Sustained or large excursion. Hardware fault, point source, or strong environmental event. |
| Dark red   | Critical | Severe or prolonged excursion. Tube failing, point source nearby, or pulse line shorted. |
| Grey       | Silent   | No post received in the last 15 minutes. Likely lost connectivity, lost power, or device failure. |

The score compares recent readings against the station's 7-day
baseline, allowing for its normal daily cycle. Cluster pins take a
weighted vote of their members, so one Critical station can tint a
small cluster; Silent and New stations never tint a cluster.

The **Events** panel on the station page lists each excursion with the
same badge as the pin colour, plus direction (↑ above baseline, ↓
below), duration, peak score and `(ongoing)` if still active.
**Offline events** use a separate grey badge with no severity.

## Opt-out at any time

Set sharing to **Off** in the device's `/webapi` page. Posts stop
immediately. Your existing station remains on the map but its
"last seen" timestamp stops advancing; it falls off the active list
after the server-side staleness window. Use **Forget station**
instead if you want the server to actively delete it.
