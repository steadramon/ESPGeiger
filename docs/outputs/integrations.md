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
| `loss` | Packet loss on the feed, as a percentage (UDP-receiver builds only; appears once a producer has been heard) |
| `tick` | Device load indicator: smoothed microseconds spent in the once-per-second housekeeping pass. Lower is better; brief spikes (e.g. an MQTT publish) are averaged out. |
| `t_max` | The worst-case `tick` seen in the last 60 seconds. Pair with `tick` to spot occasional load peaks. |
| `lps` | Main-loop iterations counted in the last second (higher is healthier). |

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

The first two values are the current **CPM** and **CPS**. The remaining ten fields are reported as `nan` - GeigerLog ignores them and plots only the two populated variables.

### Notes

* Polling is driven by GeigerLog - ESPGeiger responds on each request, so cadence is whatever you configure on the GeigerLog side.
* The endpoint is always available; there is no toggle in the ESPGeiger portal.
* If you need richer data (5-minute or 15-minute CPM, HV, memory, RSSI) consume `/json` instead from your own tooling.

## Prometheus (native `/metrics`)

ESPGeiger exposes a native [Prometheus text exposition](https://prometheus.io/docs/instrumenting/exposition_formats/) endpoint at `/metrics`. No proxy or exporter needed.

### Scrape config

```yaml
# prometheus.yml
scrape_configs:
  - job_name: espgeiger
    metrics_path: /metrics
    scrape_interval: 30s
    static_configs:
      - targets:
          - 192.168.1.100   # your device(s)
          - 192.168.1.101
```

For automatic discovery, each device advertises a `_geiger._tcp` mDNS service on port 80 (alongside the standard `_http._tcp`), so you can point your service-discovery layer at `_geiger._tcp` to find every ESPGeiger on the network.

### Sample output

```
# HELP device_info Device metadata; value always 1. Join via on(chipid) for naming.
# TYPE device_info gauge
device_info{chipid="d5536d",name="Garage",model="ESPGeiger-HW",ver="0.11.0",env="espgeigerhw"} 1

# HELP geiger_cpm Counts per minute (window label selects averaging period)
# TYPE geiger_cpm gauge
geiger_cpm{chipid="d5536d",window="1m"} 31.50
geiger_cpm{chipid="d5536d",window="5m"} 30.20
geiger_cpm{chipid="d5536d",window="15m"} 29.80

# HELP geiger_cps Instantaneous counts per second
geiger_cps{chipid="d5536d"} 0.52

# HELP geiger_usv_per_hour Dose rate in microsieverts per hour
geiger_usv_per_hour{chipid="d5536d"} 0.2086

# HELP geiger_lifetime_clicks_total Total clicks counted over device life
# TYPE geiger_lifetime_clicks_total counter
geiger_lifetime_clicks_total{chipid="d5536d"} 1845231

# HELP device_uptime_seconds Seconds since boot
device_uptime_seconds{chipid="d5536d"} 90523

# HELP device_free_heap_bytes Free heap memory
device_free_heap_bytes{chipid="d5536d"} 19432

# HELP device_wifi_rssi_dbm WiFi signal strength
device_wifi_rssi_dbm{chipid="d5536d"} -54

# HELP geiger_pulse_interval_seconds Inter-pulse intervals in seconds, log2 buckets
# TYPE geiger_pulse_interval_seconds histogram
geiger_pulse_interval_seconds_bucket{chipid="d5536d",le="0.000064"} 0
geiger_pulse_interval_seconds_bucket{chipid="d5536d",le="0.000128"} 0
...
geiger_pulse_interval_seconds_bucket{chipid="d5536d",le="+Inf"} 1845231
geiger_pulse_interval_seconds_count{chipid="d5536d"} 1845231
```

### Label model

Operational metrics carry only `chipid` - a stable device ID that never changes. Friendly name, model and firmware version live in the separate `device_info` metric. This means:

* Renaming a device in the portal does **not** fork its historical time series.
* Dashboard joins use `on(chipid) group_left(name)` to bring in the friendly name.

### PromQL examples

```promql
# Current CPM with friendly name as legend
geiger_cpm{window="1m"} * on(chipid) group_left(name) device_info

# Dose rate averaged over 1 hour
avg_over_time(geiger_usv_per_hour[1h])
  * on(chipid) group_left(name) device_info

# Alert when any device exceeds 1 µSv/h sustained for 5 min
- alert: RadiationHigh
  expr: avg_over_time(geiger_usv_per_hour[5m]) > 1
  labels:
    severity: warning
  annotations:
    summary: "{{ $labels.chipid }} dose rate sustained above 1 µSv/h"

# Devices that haven't reported in 5 min
time() - timestamp(geiger_cpm{window="1m"}) > 300
```

### Notes

* The endpoint is always available; there is no toggle in the portal.
* Histogram bucket boundaries are powers of two starting at 64 µs (inter-pulse interval). The top bucket is `+Inf`.
* `geiger_lifetime_clicks_total` is a Prometheus counter (monotonically increasing); use `rate()` for click rate.

---

## Prometheus via Telegraf

If you'd rather pull from the `/json` endpoint (eg you already run Telegraf, or you want to add fields not in `/metrics`), [Telegraf](https://www.influxdata.com/time-series-platform/telegraf/) can poll JSON and expose Prometheus metrics:

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
