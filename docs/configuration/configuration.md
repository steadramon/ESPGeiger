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
Model | String 0..32 | Varies | Model name of the ESPGeiger

## MQTT Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
IP | String 0..32 | None | The IP address of the MQTT server
Port | Int 0..65535 | `1883` |The port of the MQTT server
User | String 0..32 | None | The username used to connect to MQTT
Password | String 0..32 | None | The password used to connect to MQTT
Root Topic | String 0..32 | `ESPGeiger-{id}` | The root topic for MQTT. `{id}` is interpolated to the devices' ID.

## HA Autodiscovery Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Send | Boolean | True | Invoke Homeassistant Autodiscovery on MQTT connection
Discovery Topic | String 0..32 | `homeassistant` | Homeassistant Autodiscovery MQTT topic

## Radmon.org Configuration

## Thingspeak Configuration

## GMC Configuration

