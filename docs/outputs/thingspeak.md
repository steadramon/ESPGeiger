---
layout: page
title: ThingSpeak
permalink: /output/thingspeak
parent: Outputs
nav_order: 10
---

# ThingSpeak Output

ESPGeiger can be configured to send to a ThingSpeak Channel. ESPGeiger updates the channel every 90 seconds. This is intended that you will always be below the free account limit.

![ThingSpeak](/img/thingspeak.png)

## Setup

1. __Register on ThingSpeak__: Create an account on the [thingspeak.com](https://thingspeak.com/) website.
2. __Setup New Channel__: Login to your thingspeak.com account. Click "New Channel" from the Channels page.
3. __Configure New Channel__: Name and Describe the Channel. Set up the Field variables as below.
4. __Save New Channel__: Click __Save Channel__. Make a note of the Channel ID.
5. __Configure ESPGeiger__: In the ESPGeiger web interface, click __Config__ and enter your thingspeak.com `Channel ID` in the relevant fields.

## Field Configuration

| Field | Description |
|---|---|
| Field 1 | `CPM` |
| Field 2 | `μSv` |
| Field 3 | `CPM5` |
| Field 4 | `CPM15` |
| Field 5 | `Temperature` (°C, when an [environment sensor](/configuration/env) is present) |
| Field 6 | `Humidity` (%, BME280 / AHT family only) |
| Field 7 | `Pressure` (hPa, BME280 / BMP280 only) |

Fields 5-7 are only sent when the relevant sensor channel is available. Leave them unconfigured in your ThingSpeak channel if you don't have an environment sensor wired in.
