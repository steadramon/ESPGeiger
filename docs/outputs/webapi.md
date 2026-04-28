---
layout: page
title: ESPGeiger Stations
permalink: /output/webapi
parent: Outputs
nav_order: 20
published: true
---

# ESPGeiger Stations — Public WebAPI

ESPGeiger can optionally contribute its readings to the public map at
[stations.espgeiger.com](https://stations.espgeiger.com), a global
radiation-monitoring grid built from community-run Geiger counters.
Participation is opt-in, signed end-to-end, and privacy-respecting.

## What gets sent

The device speaks **MessagePack** to the server — a compact binary
encoding chosen to keep heap pressure low on ESP8266. The schemas
below describe the logical contents; on the wire they're encoded as
fixmaps with single- or two-letter keys.

**Per-minute post** (CPM Readings mode):

| Field | Type    | Description |
|---|---|---|
| `id`  | uint32  | Station ID (assigned at first handshake) |
| `n`   | uint32  | Unix timestamp of the reading (seconds) |
| `cpm` | float32 | Current CPM |
| `usv` | float32 | Microsieverts per hour |
| `hv`  | float32 | HV tube reading (ESPGeiger-HW only) |

In **Heartbeat** mode the radiation fields (`cpm`/`usv`/`hv`) are omitted
— the post becomes a minimal `{id, n}` body so the server still sees the
station as alive without any readings leaving the device.

**Every 15 minutes** (health snapshot, piggy-backed on the post above):

| Field  | Type   | Description |
|---|---|---|
| `ut`   | uint32 | Uptime in seconds |
| `mem`  | uint32 | Free heap (bytes) |
| `lps`  | uint32 | Loop iterations per second |
| `tk`   | uint32 | Peak ticker duration in the last 60 s (µs) |
| `rssi` | int32  | WiFi signal strength (dBm) |

Health fields live only in the raw tier server-side — they age out and
are never aggregated into long-term history.

**Handshake** (once per hour, or on first boot / reset):

| Field | Type    | Description |
|---|---|---|
| `n`   | uint32  | Unix timestamp of the handshake (seconds) |
| `pk`  | string  | Device public key, base64 (ECC secp192r1, 64 chars) |
| `cid` | string  | Device chip ID (hex). The server looks up or creates a station for this chip and returns the numeric `id` the device quotes in subsequent posts. |
| `v`   | string  | Firmware release version — `tag/sha` when built from a non-tagged commit (e.g. `devel/7a2407d`), or just `tag` for released builds (e.g. `0.9.3`). |
| `btd` | string  | Chip model (e.g. `esp8266`, `ESP32-C3`) |
| `gm`  | string  | PlatformIO build environment name (e.g. `espgeigerlog_serial`) |
| `gc`  | string  | Geiger counter model — auto-detected on Serial builds, user-set on Pulse builds |
| `fl`  | uint32  | Feature-flag bitmask (which optional output modules are compiled in) |
| `rr`  | uint32  | Normalised last-reset reason |
| `m`   | uint8   | Sharing mode: `1` = heartbeat-only, `2` = full readings (mode `0` doesn't handshake) |
| `lat` | float32 | Optional. Latitude in degrees (−90..90), 2-decimal-place rounded on device for privacy (~1.1 km cells). Omitted when both `lat` and `lon` are `0` (the "use IP" signal). |
| `lon` | float32 | Optional. Longitude in degrees (−180..180). Same rounding and omission rules as `lat`. |

The handshake is what registers your station on the map and keeps the
server's metadata (version, board, mode, etc.) current. The server
responds with the assigned station ID, which the device then uses as
`id` in subsequent per-minute posts.

## Configuring on the device

The `/webapi` page on each device shows the Station Network controls:

![Station Network config page](/img/station-network.png)

- **Station ID** — assigned by the server on first handshake. Click to view your station on `stations.espgeiger.com`.
- **Sharing** — tri-state: Off / Heartbeat / CPM Readings (see below).
- **Latitude / Longitude** — optional. Leave both at `0` to fall back to coarse IP-based geolocation. Coordinates are rounded to 2 decimal places (~1 km resolution) before publication.
- **Find location** — uses browser geolocation to fill in the lat/lon fields.
- **Advanced** — `Reset key` (rotate identity, register as a new station) and `Forget station` (server-side delete; see below).

## Sharing modes

A single tri-state pref (`webapi.mode`, default `2`) controls what
leaves the device:

| Mode | Label | Behaviour |
|---|---|---|
| `2` | **CPM Readings** *(default)* | Hourly handshake, per-minute posts with `cpm`/`usv`, 15-min health snapshots. Your station appears on the map. |
| `1` | **Heartbeat**                | Hourly handshake + 15-min health snapshots only. No radiation data leaves the device. Your station appears in fleet-size / uptime stats but not on the map. |
| `0` | **Off**                      | Nothing sent — no handshake, no posts. |

Switching modes is a single click on the device's `/webapi` page. The
posting cadence changes immediately; the server learns the new mode at
the next handshake (within an hour, declared via the handshake's `m`
field).

## Identity and signing

Each device generates an ECC keypair (secp192r1) on first run and stores
the private key in LittleFS. Every post and handshake is SHA-256'd and
signed; the signature is base64-encoded P1363 (`r||s`) and sent in the
`X-Auth` header. The server keeps your public key from the first
handshake and refuses any post whose signature doesn't verify. This
means:

- Your station can't be impersonated.
- Losing the device's flash means a new keypair on next boot, which
  registers as a new station (same chip ID, new server-side row).
- No shared secrets to configure — no passwords to leak.

### Reset key

The `/webapi` page's **Advanced → Reset key** button wipes the local
private key and reboots. The next handshake registers a fresh station;
the previous one is orphaned and eventually retired by server-side
staleness rules. Useful if you want to migrate to a new identity but
keep contributing data.

### Forget station

The `/webapi` page's **Advanced → Forget station** button sends a signed
delete request to `/api/1/forget`. The server purges the station row,
all readings, and all history. Sharing is then automatically switched
to **Off**. This is irreversible — re-enabling later registers a
brand-new station with no link to the deleted one. This is the GDPR
right-to-erasure path.

## Map presence

Once you've submitted a handshake, visit
`https://stations.espgeiger.com/station/<id>` to see your station. Each
station also gets a memorable three-word alias — e.g.
`curie-counts-photons` — which you can share directly:
`https://stations.espgeiger.com/station/curie-counts-photons`.

## Pin colours

Each station is represented by a teardrop pin on the map. The colour
reflects the station's **current reading compared to its own baseline**
— not a global threshold. This means a tube with a high background
(e.g. sitting next to granite) is only flagged when it deviates from
its *own* typical rate, not against an arbitrary fleet-wide value.

| Colour | State | Meaning |
|---|---|---|
| Blue   | New       | Fewer than ~4h of data. Baseline not yet established. |
| Green  | Normal    | Reading within 2σ of the station's baseline. |
| Amber  | Elevated  | Reading 2σ–5σ *above* baseline. Worth a glance. |
| Red    | Excursion | Reading ≥5σ *above* baseline. Unusual — investigate. |
| Grey   | Silent    | Reading ≥2σ *below* baseline, or consistent zeros with enough history to know it's not a fresh station. Likely dead tube / HV drop / pulse line disconnected. |

The score is a **Poisson z-score**: `(observed - baseline) / √baseline`.
Geiger counts follow Poisson statistics (variance = mean), so a
5σ deviation is genuinely rare for a healthy tube. Refresh cadence
is 10 minutes server-side.

**Cluster pins** (shown when many stations overlap at low zoom) inherit
the "worst" alert colour from their members: any `Excursion` child
makes the whole cluster red; otherwise any `Elevated` child makes it
amber. `Silent` and `New` are deliberately ignored at cluster level —
a single dead tube shouldn't tint the neighbourhood.

## Endpoint

The canonical URL is `http://api.espgeiger.com/api/1/`. HTTP-only is
intentional — signatures provide tamper-resistance, TLS would just add
heap pressure on ESP8266 without additional security in this threat
model.

Each request sends:

- `Content-Type: application/msgpack`
- `X-Auth: <base64(P1363 signature over the raw request body)>`
- `User-Agent: ESPGeiger/<version> <chip> <build>`

The `Accept` header is omitted; the server infers the response
encoding from `Content-Type`.

Routes:

| Path             | Method | Purpose |
|---|---|---|
| `/api/1/handshake` | POST | Register or refresh station metadata |
| `/api/1/post`      | POST | Submit a reading (and optionally a health snapshot) |
| `/api/1/forget`    | POST | Right-to-erasure — purge station + all history |

## Opt-out at any time

Set sharing to **Off** in the device's `/webapi` page. Posts stop
immediately. Your existing station remains on the map but its
"last seen" timestamp stops advancing; it falls off the active list
after the server-side staleness window. Use **Forget station**
instead if you want the server to actively delete it.
