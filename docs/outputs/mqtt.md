---
layout: page
title: MQTT
permalink: /output/mqtt
parent: Outputs
nav_order: 10
---

# MQTT

ESPGeiger publishes radiation statistics on a minutely interval. MQTT connection is performed by default on port `1883`.

By default ESPGeiger publishes to the base topic `ESPGeiger-<device_id>` - this can be changed in configuration.

| Topic | Example Value | Publish Interval | Description |
|---|---|---|---|
`ESPGeiger-<device_id>/tele/lwt` | `Online` | - | LWT topic - shows current state of ESPGeiger connection to MQTT
`ESPGeiger-<device_id>/stat/CPM` | `30` | 60 | Current CPM value
`ESPGeiger-<device_id>/stat/CPM5` | `30` | 60 | Current CPM5 value
`ESPGeiger-<device_id>/stat/CPM15` | `30` | 60 | Current CPM15 value
`ESPGeiger-<device_id>/stat/uSv` | `0.10` | 60 | Current μSv value
`ESPGeiger-<device_id>/tele/status` | `{"uptime":"2T01:45:10","board":"ESP32","model":"GC10next","free_mem":191552,"ssid":"Wifi","ip":"192.168.1.123","rssi":-24}` | 60 | Current status of board
