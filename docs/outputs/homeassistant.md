---
layout: page
title: Home Assistant
permalink: /output/ha
parent: Outputs
nav_order: 10
---

# Home Assistant MQTT Discovery

![Img](../img/ESPGeiger-Homeassistant.png)

ESPGeiger automatically integrates into [Home Assistant](https://www.home-assistant.io/) through the use of the [MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery) functions of Home Assistant. This requires ESPGeiger and Home Assistant to be configured to use the same MQTT broker.

Within Home Assistant MQTT discovery is enabled by default. The default prefix for the discovery topic is `homeassistant`. For information on how to change this in Home Assistant, please see the Home Assistant Documentation for [discovery options](https://www.home-assistant.io/integrations/mqtt/#discovery-options).

The MQTT discovery topic can be adjusted within the ESPGeiger [Config pages](/configuration#mqtt-configuration).

## Measurement entities

<!-- gen:ha sensor -->

| Value | Unit | Description | Example | Publish Interval |
|---|---|---|---|---|
| `ESPGeiger-<device_id> CPM` | CPM | Current CPM value | `30.0` | 60 s |
| `ESPGeiger-<device_id> CPM5` | CPM | 5-minute smoothed CPM | `30.0` | 60 s |
| `ESPGeiger-<device_id> CPM15` | CPM | 15-minute smoothed CPM | `30.0` | 60 s |
| `ESPGeiger-<device_id> µSv/h` | µSv/h | μSv/h value | `0.10` | 60 s |
| `ESPGeiger-<device_id> HV` | V | High-voltage reading - ESPGeiger-HW only | `400.1` | 60 s |
| `ESPGeiger-<device_id> Temperature` | °C | Temperature in °C - [environment sensor](/configuration/env) only | `22.4` | 60 s |
| `ESPGeiger-<device_id> Humidity` | % | Relative humidity in % - BME280 / AHT family only | `45.6` | 60 s |
| `ESPGeiger-<device_id> Pressure` | hPa | Atmospheric pressure in hPa - BME280 / BMP280 only | `1013.2` | 60 s |
| `ESPGeiger-<device_id> Total Clicks` | | Cumulative clicks since boot. | `43200` | 60 s |

<!-- /gen -->

## Threshold alerts (binary sensors)

Triggered when CPM crosses the configured warning / alert thresholds. Consumed via `value_template` from `tele/sensor` JSON. Good targets for HA automations (notifications, lights, sirens).

<!-- gen:ha binary -->

| Value | Description | Device class | Builds |
|---|---|---|---|
| `ESPGeiger-<device_id> Warning` | CPM above configured warning threshold | `problem` | all |
| `ESPGeiger-<device_id> Alert` | CPM above configured alert threshold | `safety` | all |
| `ESPGeiger-<device_id> Serial Connected` | Serial counter has reported within the last 60 s. | `connectivity` | serial builds |
| `ESPGeiger-<device_id> Tube Active` | Pulses are arriving from the tube (or, on UDP builds, from the source device). Takes the place of Serial Connected, which needs an external peer. | `connectivity` | pulse, UDP and test builds |

<!-- /gen -->

## Diagnostic entities

Appear under the device's **Diagnostic** section in Home Assistant, not the main dashboard.

<!-- gen:ha diag -->

| Value | Unit | Description | Example | Publish Interval |
|---|---|---|---|---|
| `ESPGeiger-<device_id> tick` | µs | Smoothed device-load figure in µs (time in the once-per-second housekeeping pass) | `953` | 60 s |
| `ESPGeiger-<device_id> tick max` | µs | Peak `tick` observed in last 60 s | `4870` | 60 s |
| `ESPGeiger-<device_id> LPS` | 1/s | Loop iterations per second | `58733` | 60 s |
| `ESPGeiger-<device_id> RSSI` | dBm | WiFi signal strength (dBm) | `-37` | 60 s |
| `ESPGeiger-<device_id> Uptime` | s | Seconds since boot | `120` | 60 s |
| `ESPGeiger-<device_id> Free Memory` | B | Free heap in bytes | `16928` | 60 s |

<!-- /gen -->


## Domoticz Autodiscovery

> From Stable 2022.1 Domoticz MQTT supports Home assistant MQTT Discovery

The Domoticz Autodiscovery Client Gateway is compatible with HomeAssistant autodiscovery. For more information see the [Domoticz wiki](https://www.domoticz.com/wiki/MQTT#Add_hardware_.22MQTT_Auto_Discovery_Client_Gateway.22)
