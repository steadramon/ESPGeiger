---
layout: page
title: Configuration
permalink: /configuration
has_children: true
nav_order: 4
---

# Configuration

There are a number of configuration options available through the ESPGeiger portal.

## Hardware Settings

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Model | String 0..32 | `(varies)` | Model name of the ESPGeiger
Ratio for calculating μSv | Float 0..10000 | `151.0` | Ratio used internally for calculating μSv
Display timeout | Int 0..65535 | `300` | Timeout for OLED display (Only for builds with OLED display)
Neopixel Brightness | Int 0..255 | `32` | Brightness for NeoPixel (Only for builds with Neopixel)

## MQTT Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
IP | String 0..32 | `null` | The IP address of the MQTT server
Port | Int 0..65535 | `1883` |The port of the MQTT server
User | String 0..32 | `null` | The username used to connect to MQTT
Password | String 0..32 | `null` | The password used to connect to MQTT
Root Topic | String 0..32 | `ESPGeiger-{id}` | The root topic for MQTT. `{id}` is interpolated to the devices' ID.

## HA Autodiscovery Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Send | Boolean | `True` | Invoke Homeassistant Autodiscovery on MQTT connection
Discovery Topic | String 0..32 | `homeassistant` | Homeassistant Autodiscovery MQTT topic

## Radmon.org Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Send | Boolean | `True` | Send data to Radmon.org
Username | String 0..32 | `null` | Radmon.org username
Password | String 0..32 | `null` | Radmon.org password

## Thingspeak Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Send | Boolean | `True` | Send data to Thingspeak
Channel Key | String 0..32 | `null` | Channel key for Thingspeak

## GMC Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Send | Boolean | `True` | Send data to GMC
Account ID | String 0..32 | `null` | gmcmap.com Account ID
Geiger Counter ID | String 0..32 | `null` | gmcmap.com Geiger Counter ID
