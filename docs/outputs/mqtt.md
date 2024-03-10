---
layout: page
title: MQTT
permalink: /output/mqtt
parent: Outputs
nav_order: 10
---

# MQTT

ESPGeiger uses MQTT to publish radiation data from your Geiger counter. This section details its interaction with MQTT brokers.

- ESPGeiger transmits radiation statistics every minute.
- By default, it connects to the MQTT broker on the standard port `1883`
- ESPGeiger publishes data to a base topic named: `ESPGeiger&#x2011;<device_id>`

## Published Topics

| Topic | Description |  Example Value | Publish Interval |
|---|---|---|---|
`ESPGeiger&#x2011;<device_id>/tele/lwt` | Last Will and Testament (LWT) topic. Indicates ESPGeiger's connection status to the MQTT broker. | `Online` | -
`ESPGeiger&#x2011;<device_id>/stat/CPM` | Current Counts Per Minute (CPM) value. | `30.0` | 60
`ESPGeiger&#x2011;<device_id>/stat/CPM5` | Average CPM over the last 5 minutes. | `30.0` | 60
`ESPGeiger&#x2011;<device_id>/stat/CPM15` | Average CPM over the last 15 minutes. | `30.0` | 60
`ESPGeiger&#x2011;<device_id>/stat/uSv` | Current microSieverts per hour (μSv) value. | `0.10` | 60
`ESPGeiger&#x2011;<device_id>/tele/status` | Current status of ESPGeiger in JSON format. | `(status_json)` | 60

## Example Status JSON Output

```
{"uptime":"2T01:45:10","board":"ESP32","model":"GC10next","free_mem":191552,"ssid":"Wifi","ip":"192.168.1.123","rssi":-24}
````