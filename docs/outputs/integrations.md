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
{"ut":66080,"c":30.00,"c5":26.19,"c15":27.55,"s":0.1714,"cs":0.50,"r":175.00,"tc":29099,"mem":22447,"rssi":-69,"tube":1,"sat":0,"warn":0,"alert":0,"health":"ok","hv":394,"hv_fault":0,"t":20.78,"tu":0,"h":48.20,"p":1027.40}
```

| Field | Description |
|---|---|
| `ut` | Uptime in seconds |
| `c` | Current CPM |
| `c5` | 5-minute smoothed CPM |
| `c15` | 15-minute smoothed CPM |
| `s` | Current µSv/h |
| `cs` | Current CPS |
| `r` | µSv conversion ratio |
| `tc` | Total clicks since boot |
| `mem` | Free heap memory in bytes |
| `rssi` | WiFi signal strength in dBm, `0` while the WiFi link is down |
| `tube` | `1` while counts are arriving, `0` once the tube has been silent longer than expected for its count rate |
| `sat` | `1` when the count rate is close to the tube's dead-time limit, so readings will under-report |
| `warn` | `1` when CPM is above the warning threshold (`counter.warn`) |
| `alert` | `1` when CPM is above the alert threshold (`counter.alert`) |
| `health` | `ok`, or what is wrong, comma split: `silent` (no counts), `storm` (an input flood, parked), `chatter` (the input rings, check tube/HV/wiring), `no_input` (serial or UDP source not reporting), `hv_low`, `hv_high`, `hv_fb` (HV builds). The device's own: `crashed` (this boot follows a crash), `noclock` (clock not synced 10 minutes after boot, with WiFi on), `savefail` (the last settings save failed), `lowheap` (free heap under 4 KB on ESP8266, 16 KB on ESP32). Names are cut to whole ones if too many are on. |
| `hv` | HV reading in whole volts (builds with HV feedback: ESPGeiger-HW, `*_hv`) |
| `hv_fault` | `1` while HV is held off: over the voltage limit (same builds) |

#### Environmental sensor

Present only when a BME280, BMP280, AHTxx or AHT20 + BMP280 combo sensor is detected. Each field is left out when the sensor does not measure it, for example `h` on a BMP280.

| Field | Description |
|---|---|
| `t` | Temperature, in the unit chosen in the sensor settings |
| `tu` | Temperature unit: `0` °C, `1` °F, `2` K |
| `h` | Relative humidity, % |
| `p` | Pressure in hPa, corrected to sea level when an elevation is set |

MQTT, WebAPI, the webhook and UDP always send temperature in °C; only `/json` follows the display unit.

#### UDP receiver builds

| Field | Description |
|---|---|
| `quiet` | Seconds since the last packet from the source, `0` before the first |
| `rebind` | Times the receive socket was opened again after the feed went silent |
| `igmp` | Multicast group joins sent again |
| `igmpx` | Group joins that could not be sent (no network interface) |
| `loss` | Packet loss on the feed over the last 10 to 15 minutes, as a percentage (appears while clicks came in during that time) |

`/json?raw=1` adds memory and load diagnostics; see [HTTP endpoint schemas](/dev/http-endpoints#jsonraw1).

## GeigerLog

[GeigerLog](https://sourceforge.net/projects/geigerlog/) is a free cross-platform log/plot tool for Geiger counters. ESPGeiger exposes a GeigerLog-compatible endpoint at `/lastdata` that works with GeigerLog's built-in **WiFiServer** device (the one that polls a device URL).

### Configure GeigerLog

1. In GeigerLog open **Device > WiFiServer > Config WiFiServer Device**.
2. Set the IP address and port to the ESPGeiger device (port `80`).
3. Point the data URL at `/lastdata`.
4. Save, then connect the WiFiServer device.

The `/lastdata` endpoint returns a single comma-separated line in GeigerLog's 12-variable order. Fields the device can't provide are left empty:

```
22.50, 0.37, 21.90, , 21.40, , , , 24.50, 1013.00, 48.00, 0.0720
```

| Position | GeigerLog variable | ESPGeiger value |
|---|---|---|
| 1 | CPM | Current CPM |
| 2 | CPS | Current CPS |
| 3 | CPM1st | 5-minute CPM |
| 5 | CPM2nd | 15-minute CPM |
| 9 | Temp | Temperature, if an environment sensor is fitted |
| 10 | Press | Pressure, if fitted |
| 11 | Humid | Humidity, if fitted |
| 12 | Xtra | HV in volts (builds with HV feedback: ESPGeiger-HW, `*_hv`), otherwise dose rate in µSv/h |

Positions 4, 6, 7 and 8 (CPS1st / CPS2nd / CPM3rd / CPS3rd) are always empty.

### Notes

* Polling is driven by GeigerLog - ESPGeiger responds on each request, so cadence is whatever you configure on the GeigerLog side.
* The endpoint is always available; there is no toggle in the ESPGeiger portal.
* Environment fields appear only when a sensor is detected; otherwise those positions stay empty.

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
device_info{chipid="d5536d",name="Garage",model="ESPGeiger-HW",ver="0.14.0",env="espgeigerhw"} 1
# HELP device_uptime_seconds Seconds since boot
# TYPE device_uptime_seconds gauge
device_uptime_seconds{chipid="d5536d"} 90523
# HELP geiger_cpm Counts per minute (window label selects averaging period)
# TYPE geiger_cpm gauge
geiger_cpm{chipid="d5536d",window="1m"} 31.50
geiger_cpm{chipid="d5536d",window="5m"} 30.20
geiger_cpm{chipid="d5536d",window="15m"} 29.80
# HELP geiger_usv_per_hour Dose rate in microsieverts per hour
# TYPE geiger_usv_per_hour gauge
geiger_usv_per_hour{chipid="d5536d"} 0.2086
# HELP geiger_cps Instantaneous counts per second
# TYPE geiger_cps gauge
geiger_cps{chipid="d5536d"} 0.52
# HELP geiger_usv_ratio CPM per uSv/h conversion ratio
# TYPE geiger_usv_ratio gauge
geiger_usv_ratio{chipid="d5536d"} 151.00
# HELP geiger_lifetime_clicks_total Total clicks counted over device life
# TYPE geiger_lifetime_clicks_total counter
geiger_lifetime_clicks_total{chipid="d5536d"} 1845231
# HELP device_free_heap_bytes Free heap memory
# TYPE device_free_heap_bytes gauge
device_free_heap_bytes{chipid="d5536d"} 19432
# HELP device_wifi_rssi_dbm WiFi signal strength
# TYPE device_wifi_rssi_dbm gauge
device_wifi_rssi_dbm{chipid="d5536d"} -54
# HELP geiger_tube_alive 1 while counts arrive, 0 once silent longer than expected for the rate
# TYPE geiger_tube_alive gauge
geiger_tube_alive{chipid="d5536d"} 1
# HELP geiger_saturated 1 when the count rate is near the dead-time limit
# TYPE geiger_saturated gauge
geiger_saturated{chipid="d5536d"} 0
# HELP device_hv_volts Tube high voltage
# TYPE device_hv_volts gauge
device_hv_volts{chipid="d5536d"} 392
# HELP device_hv_fault 1 while HV is held off: over the voltage limit or a feedback fault
# TYPE device_hv_fault gauge
device_hv_fault{chipid="d5536d"} 0
# HELP env_temperature_celsius Temperature
# TYPE env_temperature_celsius gauge
env_temperature_celsius{chipid="d5536d"} 21.63
# HELP env_humidity_percent Relative humidity
# TYPE env_humidity_percent gauge
env_humidity_percent{chipid="d5536d"} 48.20
# HELP env_pressure_hpa Pressure, sea-level corrected when an elevation is set
# TYPE env_pressure_hpa gauge
env_pressure_hpa{chipid="d5536d"} 1026.38
# HELP geiger_pulse_interval_seconds Inter-pulse intervals in seconds, log2 buckets
# TYPE geiger_pulse_interval_seconds histogram
geiger_pulse_interval_seconds_bucket{chipid="d5536d",le="0.000064"} 0
geiger_pulse_interval_seconds_bucket{chipid="d5536d",le="0.000128"} 0
...
geiger_pulse_interval_seconds_bucket{chipid="d5536d",le="+Inf"} 1845231
geiger_pulse_interval_seconds_count{chipid="d5536d"} 1845231
```

### Metrics

| Metric | Type | Description |
|---|---|---|
| `device_info` | gauge | Always 1; labels carry name, model, firmware version and build |
| `geiger_cpm` | gauge | Counts per minute; `window` label is `1m`, `5m` or `15m` |
| `geiger_cps` | gauge | Counts per second |
| `geiger_usv_per_hour` | gauge | Dose rate in µSv/h |
| `geiger_usv_ratio` | gauge | CPM per µSv/h conversion ratio |
| `geiger_lifetime_clicks_total` | counter | Clicks over the device's life. Saved to flash every 6 hours and on every clean restart; a power cut loses the clicks since the last save, which Prometheus sees as a counter reset |
| `geiger_tube_alive` | gauge | `1` while counts arrive, `0` once the tube has been silent longer than expected for its count rate |
| `geiger_saturated` | gauge | `1` when the count rate is close to the tube's dead-time limit |
| `geiger_pulse_interval_seconds` | histogram | Time between pulses, cumulative since boot |
| `device_uptime_seconds` | gauge | Seconds since boot |
| `device_free_heap_bytes` | gauge | Free heap memory |
| `device_wifi_rssi_dbm` | gauge | WiFi signal strength |
| `device_hv_volts` | gauge | Tube high voltage (builds with HV feedback) |
| `device_hv_fault` | gauge | `1` while HV is held off: over the voltage limit (builds with HV feedback) |
| `env_temperature_celsius` | gauge | Temperature, always °C |
| `env_humidity_percent` | gauge | Relative humidity |
| `env_pressure_hpa` | gauge | Pressure in hPa, corrected to sea level when an elevation is set |
| `geiger_udp_loss_percent` | gauge | Packet loss on the feed over the last 10 to 15 minutes (UDP receiver builds, while clicks came in during that time) |

The `env_` metrics appear only when a sensor is detected and each only if the sensor measures it.

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

* The endpoint is always available; there is no toggle in the portal. In [safe mode](/configuration/safemode) the counting metrics and the histogram are left out.
* Histogram bucket boundaries are powers of two starting at 64 µs (inter-pulse interval). The top bucket is `+Inf`.
* `geiger_lifetime_clicks_total` is a Prometheus counter (monotonically increasing); use `rate()` for click rate.

## InfluxDB via Telegraf

[Telegraf](https://www.influxdata.com/time-series-platform/telegraf/) can poll `/json` and write to InfluxDB. Add one `field` block per [JSON field](#json-endpoint) you want:

```toml
[[inputs.http]]
  urls = ["http://192.168.1.100/json"]
  data_format = "json_v2"
  name_override = "espgeiger"
  [[inputs.http.json_v2]]
    [[inputs.http.json_v2.field]]
      path = "c"
      rename = "cpm"
      type = "float"
    [[inputs.http.json_v2.field]]
      path = "s"
      rename = "usv_h"
      type = "float"

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

Wire `poll -> http -> your destination` (MQTT, database, dashboard, webhook, etc.).

## Home Assistant

If using MQTT with Home Assistant autodiscovery enabled, ESPGeiger sensors appear automatically. See [Home Assistant](/output/ha) for details.

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
        unit_of_measurement: "µSv/h"
      - name: "ESPGeiger WiFi RSSI"
        value_template: "{{ value_json.rssi }}"
        unit_of_measurement: "dBm"
        device_class: "signal_strength"
      - name: "ESPGeiger Free Heap"
        value_template: "{{ value_json.mem }}"
        unit_of_measurement: "B"
```

## Grafana Dashboard

Once data is in Prometheus or InfluxDB, Grafana can chart it. For Prometheus, start from the [PromQL examples](#promql-examples) above, for example `geiger_cpm{window="1m"}`.
