---
layout: page
title: Stations WebAPI Protocol
permalink: /dev/webapi-protocol
nav_order: 20
parent: Testing & Development
---

# Stations WebAPI Protocol

Wire format for the [ESPGeiger Stations](/output/webapi) uploads. Posts and handshakes use **MessagePack** (compact binary). The schemas below list the logical fields.

## Per-minute post

Sent in CPM readings mode:

| Field | Type    | Description |
|---|---|---|
| `id`  | uint32  | Station ID (assigned at first handshake) |
| `n`   | uint32  | Unix timestamp of the reading (seconds) |
| `c`   | float32 | Current CPM |
| `u`   | float32 | Microsieverts per hour |
| `hb`  | uint    | Health bits, omitted while all fine: 1 no counts (dead tube or input, so `c`/`u` are not a real reading), 2 input storm, 4 chatter, 8 input not reporting, 16 HV low, 32 HV high, 64 HV feedback. Device states (crash, clock, save, heap) are not sent. Add-only. |
| `hv`  | int     | HV tube reading in volts (builds with HV feedback: ESPGeiger-HW, `*_hv`) |
| `t`   | float32 | Temperature in °C ([environment sensor](/configuration/env) only) |
| `h`   | float32 | Humidity in % (BME280 / AHT family only) |
| `p`   | float32 | Pressure in hPa (BME280 / BMP280 only) |

In **Heartbeat** mode the device posts every 15 minutes with `{id, n}` plus the health snapshot and no radiation fields.

## Health snapshot

Added to the post every 15 minutes:

| Field | Type   | Description |
|---|---|---|
| `ut`  | uint32 | Uptime in seconds |
| `mh`  | uint32 | Free heap (bytes) |
| `l`   | uint32 | Loop iterations per second |
| `tk`  | uint32 | Peak ticker duration in the last 60 s (µs) |
| `rs`  | int32  | WiFi signal strength (dBm) |
| `ct`  | float32 | Chip temperature in °C (ESP32 family only) |

Health fields live only in the raw tier server-side. They age out and are never aggregated into long-term history.

## Handshake

Sent once per hour and on first boot or reset:

| Field | Type    | Description |
|---|---|---|
| `n`   | uint32  | Unix timestamp of the handshake (seconds) |
| `pk`  | string  | Device public key, base64 (ECC secp192r1, 64 chars) |
| `ci`  | string  | Device chip ID (hex). The server looks up or creates a station for this chip and returns the numeric `id` the device quotes in later posts. |
| `v`   | string  | Release version and the git revision it was built from, `version/git` (e.g. `0.14.0/7a2407d`, `devel/7a2407d`). Just the version when the revision is unknown. |
| `bd`  | string  | Board name on ESPGeiger kits (e.g. `ESPG-HW`), else the chip model (e.g. `ESP8266`, `ESP32-C3`) |
| `gm`  | string  | PlatformIO build environment name (e.g. `espgeigerlog_serial`) |
| `gc`  | string  | Geiger counter model: auto-detected on Serial builds, user-set on Pulse builds |
| `td`  | uint32  | Tube detection bitmask: bit 0 = α, bit 1 = β, bit 2 = γ. `0` = unknown / not set. Bits 3-7 reserved. |
| `tb`  | uint32  | Optional. The **Tube** setting's number: 1 SBM-20, 2 SBM-19, 3 SBM-21, 4 STS-5, 5 SI-22G, 6 J305, 7 J321, 8 M4011, 9 LND 712, 10 VacuTec 70 013; new tubes are appended. Omitted when it is Other (0). |
| `fl`  | uint32  | Feature-flag bitmask (which optional output modules are compiled in) |
| `rr`  | uint32  | Normalised last-reset reason |
| `m`   | uint8   | Sharing mode: `1` = heartbeat-only, `2` = full readings (mode `0` doesn't handshake) |
| `la`  | float32 | Optional. Latitude in degrees (-90..90), rounded on device to 2 decimal places (~1.1 km cells). Omitted when both `la` and `lo` are `0` (the "use IP" signal). |
| `lo`  | float32 | Optional. Longitude in degrees (-180..180). Same rounding and omission rules as `la`. |

After a crash the first handshake of the boot adds what the device kept about it. Each group is only sent when there is something in it:

| Field | Type    | Description |
|---|---|---|
| `ec`  | uint32  | Exception cause |
| `pc`  | uint32  | Program counter at the fault (epc1) |
| `va`  | uint32  | Fault address (excvaddr) |
| `e2`  | uint32  | epc2 |
| `e3`  | uint32  | epc3 |
| `dp`  | uint32  | depc |
| `sp`  | uint32  | Stack pointer at the fault |
| `st`  | bin     | Up to 32 words, little-endian uint32 each: on ESP8266 the code addresses found on the stack, on ESP32 the backtrace PCs |
| `cm`  | string  | Where the loop was when it crashed: a module name, `tcp` or `portal` |
| `rk`  | uint32  | Kind of reset: 5 brownout, 6 exception, 7 hardware watchdog, 8 software watchdog, 9 interrupt watchdog, 10 task watchdog, 11 watchdog, 12 abort, 13 out of memory, 14 stack smash, 15 a damaged or unknown dump. Add-only. |
| `fa`  | uint32  | Out of memory only: the caller of the allocation that failed |

The server responds with the assigned station ID, which the device uses as `id` in later posts.

The key is made on the first handshake and kept both in a file on the device and in a copy outside the file system; a lost file system does not change `pk`. **Reset key** deletes both copies. **Forget station** keeps the key and asks the server to delete the station.

## Endpoint

Canonical URL: `http://api.espgeiger.com/api/1/`. HTTP-only by design: each request is individually signed, so it cannot be tampered with in transit and TLS would cost a lot of memory on the ESP8266 for no real security gain.

| Path             | Method | Purpose |
|---|---|---|
| `/api/1/handshake` | POST | Register or refresh station metadata |
| `/api/1/post`      | POST | Submit a reading (+ optional health snapshot) |
| `/api/1/forget`    | POST | Right-to-erasure: purge station + history |

`X-Auth` is the base64 ECDSA (secp192r1) signature over SHA-256 of the path, a 0 byte, then the body, for example `/api/1/post` 0x00 body. The path ties a signature to its endpoint, so a captured post can't be sent to `/forget`. 0.13 and older sign the body alone; the server still takes that from them.

Devices don't follow redirects, so the API stays on plain http at `/api/1`. Reply codes the device acts on: `403` re-syncs NTP when its body is `Clock off`, and to a post also drops the station and handshakes again, `410` (or `404` to a handshake) stops sharing till its settings are saved or the device restarts, `429` skips the next 5 posts. Any other error is logged to the console.

## Pin scoring

The pin colour is a [Poisson CUSUM](https://en.wikipedia.org/wiki/CUSUM) detector running against the station's 7-day baseline, with a per-hour-of-day adjustment so stable daily cycles (heating, occupancy, indoor radon) are absorbed. A slack term absorbs routine Poisson noise; sustained or large excursions accumulate across consecutive 15-minute buckets and open an event when they cross the threshold. The accumulator is normalised by each station's own σ, so a 2σ excursion sustained for an hour reads the same on a 10 cpm tube and a 1000 cpm tube. Refresh cadence is 10 minutes server-side.

Cluster pins use a weighted vote: `Critical` 16, `Danger` 8, `Warning` 4, `Elevated` 2, others 1. `Silent` and `New` never tint a cluster.
