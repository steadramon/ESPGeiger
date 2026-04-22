---
layout: page
title: MQTT
permalink: /output/mqtt
parent: Outputs
nav_order: 10
---

# MQTT Output

ESPGeiger uses MQTT to publish radiation data from your Geiger counter. This section details its interaction with MQTT brokers.

- ESPGeiger transmits radiation statistics every minute. This can be configured.
- By default, it connects to the MQTT broker on the standard port `1883`
- ESPGeiger publishes data to a base topic named: `ESPGeiger‑<device_id>`

## Published Topics

### Primary topics (JSON)

| Topic | Description | Publish Interval Default |
|---|---|---|
| `ESPGeiger-<device_id>/tele/lwt` | Last Will and Testament. `Online` on connect, `Offline` via LWT on disconnect. | event |
| `ESPGeiger-<device_id>/tele/sensor` | Radiation measurements bundle (CPM, uSv, CPS, CPM5, CPM15, HV, warn, alert). | 60 s |
| `ESPGeiger-<device_id>/tele/status` | Device state + diagnostics (time, uptime, IP, RSSI, counts, load metrics, memory, serial health on serial builds). | 60 s |

### Event topics

| Topic | Description | Example | Publish Interval |
|---|---|---|---|
| `ESPGeiger-<device_id>/stat/WARN` | Triggered when CPM crosses the warning threshold. | `0` / `1` | edge-triggered |
| `ESPGeiger-<device_id>/stat/ALERT` | Triggered when CPM crosses the alert threshold. | `0` / `1` | edge-triggered |

### Legacy per-metric topics (backward compat)

Planned for removal in a future release — prefer consuming `tele/sensor` JSON instead.

| Topic | Example | Interval |
|---|---|---|
| `ESPGeiger-<device_id>/stat/CPM` | `30.00` | 60 s |
| `ESPGeiger-<device_id>/stat/CPM5` | `28.50` | 60 s |
| `ESPGeiger-<device_id>/stat/CPM15` | `29.10` | 60 s |
| `ESPGeiger-<device_id>/stat/uSv` | `0.20` | 60 s |
| `ESPGeiger-<device_id>/stat/CPS` | `0.50` | 60 s |
| `ESPGeiger-<device_id>/stat/HV` | `412.00` | 60 s (ESPGeiger-HW only) |

## Example `tele/sensor` JSON

```json
{"cpm":87.00,"usv":0.51,"cps":1.45,"cpm5":149.76,"cpm15":212.07,"warn":1,"alert":0}
```

| Field | Description |
|---|---|
| `cpm` | Current CPM |
| `cpm5` | 5-minute smoothed CPM |
| `cpm15` | 15-minute smoothed CPM |
| `usv` | μSv/h (cpm × ratio) |
| `cps` | Counts per second |
| `hv` | HV reading (ESPGeiger-HW only) |
| `warn` | `1` when CPM is above configured warning threshold |
| `alert` | `1` when CPM is above configured alert threshold |

## Example `tele/status` JSON

```json
{"time":"2026-04-18T14:52:52Z","ut":120,"board":"ESP-12E","model":"ESPGeiger-Log","ssid":"MyWiFi","ip":"192.168.1.166","rssi":-37,"c_total":165,"tick":953,"t_max":4870,"lps":58733,"free_mem":16928}
```

| Field | Description |
|---|---|
| `time` | Current device time (ISO 8601 UTC, with `Z` suffix) |
| `ut` | Uptime in seconds |
| `board` | ESP chip model |
| `model` | Configured Geiger model |
| `ssid` | Connected WiFi SSID |
| `ip` | Device IP address |
| `rssi` | WiFi signal (dBm) |
| `c_total` | Total clicks since boot |
| `tick` | EMA-smoothed sTickerCB duration (μs) — device load signal |
| `t_max` | Peak `tick` in last 60 s |
| `lps` | Loop iterations per second |
| `free_mem` | Free heap (bytes) |
| `ser_ok` | `1` when the external serial counter has sent a valid line within the last 60 s, `0` otherwise. Serial builds only — absent from pulse builds. |