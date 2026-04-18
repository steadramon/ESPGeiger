---
layout: page
title: Integrations
permalink: /output/integrations
parent: Outputs
nav_order: 20
---

# Integrations

ESPGeiger exposes radiation data through several channels (MQTT, Webhook, JSON endpoint). This page shows how to connect these to common monitoring and automation platforms.

## JSON Endpoint

The built-in web server exposes a JSON endpoint at `/json`:

```
http://192.168.1.100/json
```

Response:

```json
{"ut":5025,"c":42.00,"s":0.28,"c5":41.50,"c15":40.90,"cs":0.70,"r":151.0,"tc":9876,"mem":19088,"rssi":-45,"tick":712,"t_max":8012,"lps":59831}
```

| Field | Description |
|---|---|
| `ut` | Uptime in seconds |
| `c` | Current CPM |
| `s` | Current μSv/h |
| `c5` | 5-minute smoothed CPM |
| `c15` | 15-minute smoothed CPM |
| `cs` | Current CPS |
| `r` | μSv conversion ratio |
| `tc` | Total clicks since boot |
| `mem` | Free heap memory in bytes |
| `rssi` | WiFi signal strength in dBm |
| `hv` | HV reading in volts (ESPGeiger-HW only) |
| `tick` | EMA-smoothed duration of the 1-second tick callback in microseconds (sTickerCB cost). Typical load indicator; α = 1/8 so single-tick spikes (MQTT publish, etc.) are averaged out. Lower is better. |
| `t_max` | Peak `tick` observed in the current 60-tick window (rolling reset). Use alongside `tick` to see worst-case cost. |
| `lps` | Loop iterations counted in the last second. |

## GeigerLog

[GeigerLog](https://sourceforge.net/projects/geigerlog/) is a free cross-platform log/plot tool for Geiger counters. ESPGeiger exposes a GeigerLog-compatible endpoint at `/lastdata` that works with GeigerLog's built-in **WiFiClient** device.

### Configure GeigerLog

1. In GeigerLog open **Device → WiFiClient → Config WiFiClient Device**.
2. Set the IP address and port to the ESPGeiger device (port `80`).
3. Point the data URL at `/lastdata`.
4. Save, then connect the WiFiClient device.

The `/lastdata` endpoint returns a single comma-separated line matching the GeigerLog WiFiClient format:

```
22.50, 0.37, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan
```

The first two values are the current **CPM** and **CPS**. The remaining ten fields are reported as `nan` — GeigerLog ignores them and plots only the two populated variables.

### Notes

* Polling is driven by GeigerLog — ESPGeiger responds on each request, so cadence is whatever you configure on the GeigerLog side.
* The endpoint is always available; there is no toggle in the ESPGeiger portal.
* If you need richer data (5-minute or 15-minute CPM, HV, memory, RSSI) consume `/json` instead from your own tooling.

## Prometheus via Telegraf

[Telegraf](https://www.influxdata.com/time-series-platform/telegraf/) can poll the JSON endpoint and expose the values as Prometheus metrics:

```toml
# telegraf.conf

[[inputs.http]]
  urls = ["http://192.168.1.100/json"]
  method = "GET"
  timeout = "5s"
  data_format = "json_v2"
  name_override = "espgeiger"
  [[inputs.http.tags]]
    device = "espgeiger-xxxxxx"
  [[inputs.http.json_v2]]
    [[inputs.http.json_v2.field]]
      path = "c"
      rename = "cpm"
      type = "float"
    [[inputs.http.json_v2.field]]
      path = "s"
      rename = "usv_h"
      type = "float"
    [[inputs.http.json_v2.field]]
      path = "c5"
      rename = "cpm5"
      type = "float"
    [[inputs.http.json_v2.field]]
      path = "c15"
      rename = "cpm15"
      type = "float"
    [[inputs.http.json_v2.field]]
      path = "cs"
      rename = "cps"
      type = "float"
    [[inputs.http.json_v2.field]]
      path = "tc"
      rename = "total_clicks"
      type = "int"
    [[inputs.http.json_v2.field]]
      path = "ut"
      rename = "uptime_seconds"
      type = "int"
    [[inputs.http.json_v2.field]]
      path = "mem"
      rename = "free_heap"
      type = "int"
    [[inputs.http.json_v2.field]]
      path = "rssi"
      rename = "wifi_rssi"
      type = "int"

[[outputs.prometheus_client]]
  listen = ":9273"
```

Prometheus can then scrape `http://<telegraf-host>:9273/metrics`.

## Prometheus via MQTT Exporter

If you are already publishing via MQTT, the [mqtt2prometheus](https://github.com/hikhvar/mqtt2prometheus) exporter converts MQTT topics into Prometheus metrics:

```yaml
# mqtt2prometheus.yaml
mqtt:
  server: tcp://mqtt-broker:1883
  topic_path: ESPGeiger-+/stat/+
  device_id_regex: "ESPGeiger-(?P<deviceid>[0-9a-f]+)/stat/.+"
  qos: 0
metrics:
  - prom_name: espgeiger_cpm
    mqtt_name: CPM
    help: Current counts per minute
    type: gauge
  - prom_name: espgeiger_usv
    mqtt_name: uSv
    help: Current microsieverts per hour
    type: gauge
  - prom_name: espgeiger_cpm5
    mqtt_name: CPM5
    help: 5-minute smoothed CPM
    type: gauge
```

## InfluxDB via Telegraf

To write directly to InfluxDB instead of Prometheus:

```toml
[[inputs.http]]
  urls = ["http://192.168.1.100/json"]
  data_format = "json_v2"
  name_override = "espgeiger"
  # ... field config as above ...

[[outputs.influxdb_v2]]
  urls = ["http://influxdb:8086"]
  token = "YOUR_TOKEN"
  organization = "your-org"
  bucket = "radiation"
```

## Node-RED

Node-RED can poll the JSON endpoint and route data anywhere:

```json
[
  {
    "id": "poll",
    "type": "inject",
    "repeat": "60",
    "topic": "",
    "payload": "",
    "payloadType": "date"
  },
  {
    "id": "http",
    "type": "http request",
    "method": "GET",
    "url": "http://192.168.1.100/json",
    "ret": "obj"
  }
]
```

Wire `poll → http → your destination` (MQTT, database, dashboard, webhook, etc.).

## Home Assistant

If using MQTT with Home Assistant autodiscovery enabled, ESPGeiger sensors appear automatically. See [Home Assistant](/output/homeassistant) for details.

For REST polling without MQTT:

```yaml
# configuration.yaml
rest:
  - resource: http://192.168.1.100/json
    scan_interval: 30
    sensor:
      - name: "Radiation CPM"
        value_template: "{{ value_json.c }}"
        unit_of_measurement: "CPM"
      - name: "Radiation uSv/h"
        value_template: "{{ value_json.s }}"
        unit_of_measurement: "μSv/h"
        device_class: "radiation"
      - name: "ESPGeiger WiFi RSSI"
        value_template: "{{ value_json.rssi }}"
        unit_of_measurement: "dBm"
        device_class: "signal_strength"
      - name: "ESPGeiger Free Heap"
        value_template: "{{ value_json.mem }}"
        unit_of_measurement: "B"
```

## Grafana Dashboard

Once data is in Prometheus or InfluxDB, Grafana can visualise it. A simple dashboard query for Prometheus:

```promql
# Current CPM
espgeiger_cpm

# 5 minute average
avg_over_time(espgeiger_cpm[5m])

# Alert when above threshold
espgeiger_cpm > 100
```
