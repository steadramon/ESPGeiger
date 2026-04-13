---
layout: page
title: Webhook
permalink: /output/webhook
parent: Outputs
nav_order: 10
---

# Webhook Output

ESPGeiger can send radiation data to a custom URL endpoint via HTTP POST. This allows you to integrate your Geiger counter readings with your own services, databases or automation systems.

## Setup

1. __Configure ESPGeiger__: In the ESPGeiger web interface, click __Config__ and enter your target `URL` in the Webhook section. Optionally set a `Key` for authenticating requests from your device.
2. __Set the interval__: Configure the submission interval in seconds. The default is 60 seconds.

ESPGeiger will report the Webhook submission status in the Web Interface console.

**Note:** Only HTTP endpoints are supported. HTTPS URLs will be downgraded to HTTP.

## JSON Payload

ESPGeiger submits a JSON payload via HTTP POST with `Content-Type: application/json`. The following fields are included:

| Field | Type | Description |
|---|---|---|
| `id` | string | Device chip ID |
| `key` | string | Configured webhook key (only included if set) |
| `ut` | integer | Uptime in seconds |
| `cps` | float | Current counts per second |
| `cpm` | float | Current CPM (1-minute smoothed) |
| `cpm5` | float | 5-minute smoothed CPM |
| `cpm15` | float | 15-minute smoothed CPM |
| `usv` | float | Current microsieverts per hour (μSv/h) |
| `hv` | float | High voltage reading in volts (ESPGeiger-HW only) |
| `tc` | integer | Total accumulated click count since boot |
| `mem` | integer | Free heap memory in bytes |
| `rssi` | integer | WiFi signal strength in dBm |

## Example JSON Output

```json
{"id":"7a9256","ut":3600,"cps":0.50,"cpm":30.00,"cpm5":29.50,"cpm15":30.10,"usv":0.20,"tc":1800,"mem":19088,"rssi":-45}
```
