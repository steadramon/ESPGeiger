---
layout: page
title: ThingSpeak
permalink: /output/thingspeak
parent: Outputs
nav_order: 10
---

# ThingSpeak Output

ESPGeiger can be configured to send to a ThingSpeak Channel, updating it every 90 seconds. That cadence is chosen to keep you comfortably below the free account's update limit.

![ThingSpeak](/img/thingspeak.png)

## Setup

1. __Register on ThingSpeak__: Create an account on the [thingspeak.com](https://thingspeak.com/) website.
2. __Setup New Channel__: Login to your thingspeak.com account. Click "New Channel" from the Channels page.
3. __Configure New Channel__: Name and Describe the Channel. Set up the Field variables as below.
4. __Save New Channel__: Click __Save Channel__. Open the channel's __API Keys__ tab and copy the __Write API Key__.
5. __Configure ESPGeiger__: On the [Config page](/configuration), open the __Upload__ tab, tick __Enable__ under __ThingSpeak__ and paste the Write API Key into __Channel Key__.

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
