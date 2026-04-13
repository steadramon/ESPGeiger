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

| Topic | Description |  Example Value | Publish Interval Default |
|---|---|---|---|
`ESPGeiger⁠-⁠<device_id>⁠/⁠tele⁠/⁠lwt` | Last Will and Testament (LWT) topic. Indicates ESPGeiger's connection status to the MQTT broker. | `Online` | `Online` published when MQTT connection established. `Offline` set as LWT.
`ESPGeiger⁠-⁠<device_id>⁠/⁠stat⁠/⁠CPM` | Current Counts Per Minute (CPM) value. | `30.0` | 60
`ESPGeiger⁠-⁠<device_id>⁠/⁠stat⁠/⁠CPM5` | Average CPM over the last 5 minutes. | `30.0` | 60
`ESPGeiger⁠-⁠<device_id>⁠/⁠stat⁠/⁠CPM15` | Average CPM over the last 15 minutes. | `30.0` | 60
`ESPGeiger⁠-⁠<device_id>⁠/⁠stat⁠/⁠uSv` | Current microSieverts per hour (μSv) value. | `0.10` | 60
`ESPGeiger⁠-⁠<device_id>⁠/⁠stat⁠/HV` | Current HV circuit value - ESPGeigerHW only. | `400.1` | 60
`ESPGeiger⁠-⁠<device_id>⁠/⁠stat⁠/WARN` | Triggered when over Warning threshold. | `0` / `1` | -
`ESPGeiger⁠-⁠<device_id>⁠/⁠stat⁠/ALERT` | Triggered when over Alert threshold. | `0` / `1` | -
`ESPGeiger⁠-⁠<device_id>⁠/⁠tele⁠/⁠status` | Current status of ESPGeiger in JSON format. | `(status_json)` | 60

## Example Tele Status JSON Output

```json
{"time":"2024-01-15T14:30:00","uptime":"2T01:45:10","board":"ESP32","model":"GC10next","ssid":"Wifi","ip":"192.168.1.123","rssi":-24,"c_total":43200,"free_mem":191552}
```