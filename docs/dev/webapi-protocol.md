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

Sent in CPM Readings mode:

| Field | Type    | Description |
|---|---|---|
| `id`  | uint32  | Station ID (assigned at first handshake) |
| `n`   | uint32  | Unix timestamp of the reading (seconds) |
| `c`   | float32 | Current CPM |
| `u`   | float32 | Microsieverts per hour |
| `hv`  | float32 | HV tube reading (ESPGeiger-HW only) |
| `t`   | float32 | Temperature in °C ([environment sensor](/configuration/env) only) |
| `h`   | float32 | Humidity in % (BME280 / AHT family only) |
| `p`   | float32 | Pressure in hPa (BME280 / BMP280 only) |

In **Heartbeat** mode the radiation fields (`c`/`u`/`hv`) are omitted and the post is a minimal `{id, n}` body, so the server still sees the station as alive.

## Health snapshot

Added to the post every 15 minutes:

| Field | Type   | Description |
|---|---|---|
| `ut`  | uint32 | Uptime in seconds |
| `mh`  | uint32 | Free heap (bytes) |
| `l`   | uint32 | Loop iterations per second |
| `tk`  | uint32 | Peak ticker duration in the last 60 s (µs) |
| `rs`  | int32  | WiFi signal strength (dBm) |

Health fields live only in the raw tier server-side. They age out and are never aggregated into long-term history.

## Handshake

Sent once per hour, and on first boot or reset:

| Field | Type    | Description |
|---|---|---|
| `n`   | uint32  | Unix timestamp of the handshake (seconds) |
| `pk`  | string  | Device public key, base64 (ECC secp192r1, 64 chars) |
| `ci`  | string  | Device chip ID (hex). The server looks up or creates a station for this chip and returns the numeric `id` the device quotes in later posts. |
| `v`   | string  | Firmware release version: `tag/sha` when built from a non-tagged commit (e.g. `devel/7a2407d`), or `tag` for released builds (e.g. `0.9.3`). |
| `bd`  | string  | Chip model (e.g. `esp8266`, `ESP32-C3`) |
| `gm`  | string  | PlatformIO build environment name (e.g. `espgeigerlog_serial`) |
| `gc`  | string  | Geiger counter model: auto-detected on Serial builds, user-set on Pulse builds |
| `td`  | uint32  | Tube detection bitmask: bit 0 = α, bit 1 = β, bit 2 = γ. `0` = unknown / not set. Bits 3-7 reserved. |
| `fl`  | uint32  | Feature-flag bitmask (which optional output modules are compiled in) |
| `rr`  | uint32  | Normalised last-reset reason |
| `m`   | uint8   | Sharing mode: `1` = heartbeat-only, `2` = full readings (mode `0` doesn't handshake) |
| `la`  | float32 | Optional. Latitude in degrees (-90..90), rounded on device to 2 decimal places (~1.1 km cells). Omitted when both `la` and `lo` are `0` (the "use IP" signal). |
| `lo`  | float32 | Optional. Longitude in degrees (-180..180). Same rounding and omission rules as `la`. |

The server responds with the assigned station ID, which the device uses as `id` in later posts.

## Endpoint

Canonical URL: `http://api.espgeiger.com/api/1/`. HTTP-only by design: each request is individually signed, so it cannot be tampered with in transit, and TLS would cost a lot of memory on the ESP8266 for no real security gain.

| Path             | Method | Purpose |
|---|---|---|
| `/api/1/handshake` | POST | Register or refresh station metadata |
| `/api/1/post`      | POST | Submit a reading (+ optional health snapshot) |
| `/api/1/forget`    | POST | Right-to-erasure: purge station + history |

## Pin scoring

The pin colour is a [Poisson CUSUM](https://en.wikipedia.org/wiki/CUSUM) detector running against the station's 7-day baseline, with a per-hour-of-day adjustment so stable daily cycles (heating, occupancy, indoor radon) are absorbed. A slack term absorbs routine Poisson noise; sustained or large excursions accumulate across consecutive 15-minute buckets and open an event when they cross the threshold. The accumulator is normalised by each station's own σ, so a 2σ excursion sustained for an hour reads the same on a 10 cpm tube and a 1000 cpm tube. Refresh cadence is 10 minutes server-side.

Cluster pins use a weighted vote: `Critical` 16, `Danger` 8, `Warning` 4, `Elevated` 2, others 1. `Silent` and `New` never tint a cluster.
