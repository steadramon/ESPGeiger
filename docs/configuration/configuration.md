---
layout: page
title: Configuration
permalink: /configuration
has_children: true
nav_order: 4
---

# Configuration

Most of the options below are available from the ESPGeiger [Web Portal](/output/webportal) — click the **Config** button on the main page. A few settings have their own dedicated pages accessed from the main portal:

* **NTP Config** — time zone and NTP server settings. See [NTP Configuration](/configuration/ntp).
* **HV Config** — high-voltage tuning for ESPGeiger-HW boards. See [HV Configuration](/configuration/hv).

## Hardware Settings

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Model | String 0..32 | `(varies)` | Model name of the ESPGeiger
Ratio for calculating μSv | Float 0..10000 | `151.0` | Ratio used internally for calculating μSv
Warning CPM | Int | `50` | CPM Value to trigger Warning state
Alert CPM | Int | `100` | CPM Value to trigger Alert state
RX Pin | Int 0..99 | `(varies)` | Geiger counter input pin. Hidden on builds compiled with `-D RXPIN_BLOCKED`.
TX Pin | Int 0..99 | `(varies)` | Transmit pin, only shown on test builds with a TX pin configured. Hidden when `-D TXPIN_BLOCKED` is set.
PCNT Filter | Int 0..1023 | `100` | Glitch filter threshold in APB clock cycles (12.5ns each). `0` disables. Only shown on ESP32 PCNT builds. See [PCNT Filter](/hardware/esphardware#pcnt-filter).
PCNT Pin Pull | Int 0..2 | `1` | Internal pull on the PCNT input pin: `0`=floating (no pull), `1`=pull-up (default), `2`=pull-down. Most active-low modules work with `1`. If you see missed counts, try toggling. Only shown on ESP32 PCNT builds. Compile-time default can be set via `-D PCNT_FLOATING_PIN` or `-D PCNT_PULLDOWN_PIN`.
Debounce (μs) | Int 0..10000 | `500` | Software interrupt debounce window in microseconds. `0` disables. Only shown on pulse builds that use software interrupt counting (ESP8266, or ESP32 `no_pcnt`). See [Interrupt Debounce](/hardware/esphardware#interrupt-debounce).
Display brightness | Int 0..100 | `25` | Brightness of the OLED Display (Only for builds with OLED display)
Display timeout | Int 0..65535 | `300` | Timeout for OLED display (Only for builds with OLED display and push button)
On Time | Time HH:MM | `06:00` | OLED on-time (shown on OLED builds without a push button)
Off Time | Time HH:MM | `22:00` | OLED off-time (shown on OLED builds without a push button)
Neopixel Brightness | Int 0..100 | `10` | % Brightness for NeoPixel - 0 disables (Only for builds with Neopixel, note too low a value can cause colours to be inaccurate)

## MQTT Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
IP | String 0..32 | `null` | The IP address of the MQTT server
Port | Int 0..65535 | `1883` |The port of the MQTT server
User | String 0..32 | `null` | The username used to connect to MQTT
Password | String 0..32 | `null` | The password used to connect to MQTT
Root Topic | String 0..32 | `ESPGeiger-{id}` | The root topic for MQTT. `{id}` is interpolated to the devices' ID.
Submit Time (s) | Int 5-3600 | `60` | Interval in seconds to post `/stat` topic values

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

## Webhook Configuration

| Setting name | Value Range | Default | Description |
|---|---|---|---|
Send | Boolean | `True` | Send data to the configured webhook
Webhook URL | String 0..255 | `null` | Full URL to POST data to. See [Webhook](/output/webhook).
Webhook Key | String 0..255 | `null` | Optional key sent with each request. Omitted when empty.
Submit Time (s) | Int 1..3600 | `60` | Interval in seconds between webhook submissions
