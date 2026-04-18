---
layout: page
title: Configuration
permalink: /configuration
has_children: true
nav_order: 4
---

# Configuration

Most settings are available from the ESPGeiger [Web Portal](/output/webportal) — click **Config** on the main page.

Specialised pages with their own UI:

* **NTP Config** — time zone and NTP server. See [NTP Configuration](/configuration/ntp).
* **HV Config** — high-voltage tuning for ESPGeiger-HW boards. See [HV Configuration](/configuration/hv).

Settings changed on the Config page take effect immediately (or on next submission cycle for output modules). Pin and serial type changes require a reboot.

## System Settings

| Setting | Type | Default | Description |
|---|---|---|---|
| uSv/h Ratio | Float | `151.0` | CPM to uSv/h conversion factor |
| Warning CPM | Int 0-9999 | `50` | CPM threshold for warning state |
| Alert CPM | Int 0-9999 | `100` | CPM threshold for alert state |
| Serial Type | Int 1-255 | `(varies)` | Serial counter protocol: 1=GC10, 2=GC10Next, 3=MightyOhm, 4=ESPGeiger. Only shown on serial builds. Reboot required. |
| RX Pin | Int | `(varies)` | Geiger counter input pin. Reboot required. Hidden when `-D RXPIN_BLOCKED`. |
| TX Pin | Int | `(varies)` | Transmit pin. Only on builds with a TX pin configured. Reboot required. |
| PCNT Filter | Int 0-1023 | `100` | Glitch filter threshold (ESP32 PCNT builds only). `0` disables. See [PCNT Filter](/hardware/esphardware#pcnt-filter). |
| PCNT Pin Pull | Int 0-2 | `1` | PCNT input pin pull: `0`=none, `1`=up, `2`=down. ESP32 PCNT builds only. |
| Debounce (us) | Int 0-10000 | `500` | Software interrupt debounce. Pulse builds without PCNT only. |

## MQTT Configuration

| Setting | Type | Default | Description |
|---|---|---|---|
| Server | String (16) | `(empty)` | MQTT broker address or IP. Leave empty to disable MQTT. |
| Port | Int 1-65535 | `1883` | MQTT broker port |
| User | String (32) | `(empty)` | MQTT username |
| Password | String (32) | `(empty)` | MQTT password (sensitive) |
| Root Topic | String (16) | `ESPGeiger-{id}` | Root publish topic. `{id}` is replaced with the chip ID. |
| Interval | Int 5-3600 | `60` | Publish interval in seconds |
| HA Autodiscovery | Boolean | `true` | Publish Home Assistant autodiscovery on connect |
| HA Discovery Topic | String (32) | `homeassistant` | Home Assistant discovery prefix |

## Radmon.org Configuration

| Setting | Type | Default | Description |
|---|---|---|---|
| Enable | Boolean | `false` | Upload to radmon.org |
| Username | String (32) | `(empty)` | Radmon.org username |
| Password | String (64) | `(empty)` | Radmon.org password (sensitive) |
| Interval | Int 30-1800 | `60` | Upload interval in seconds |

## ThingSpeak Configuration

| Setting | Type | Default | Description |
|---|---|---|---|
| Enable | Boolean | `false` | Upload to ThingSpeak |
| Channel Key | String (16) | `(empty)` | ThingSpeak channel write API key (sensitive) |

## GMC Configuration

| Setting | Type | Default | Description |
|---|---|---|---|
| Enable | Boolean | `false` | Upload to gmcmap.com |
| Account ID | String (12) | `(empty)` | gmcmap.com account ID (numeric) |
| Geiger Counter ID | String (12) | `(empty)` | gmcmap.com geiger counter ID (numeric) |

## Webhook Configuration

| Setting | Type | Default | Description |
|---|---|---|---|
| Enable | Boolean | `false` | POST measurements to a webhook |
| URL | String (255) | `(empty)` | Webhook endpoint (http only) |
| Key | String (255) | `(empty)` | Optional shared secret sent as "key" in payload (sensitive) |
| Interval | Int 10-3600 | `60` | POST interval in seconds |

## SD Card Settings (SD-enabled builds)

| Setting | Type | Default | Description |
|---|---|---|---|
| Sync Interval (min) | Int 1-5 | `1` | Minutes between syncs to the card. `1` syncs on every write (safest). Higher values reduce SD wear at the cost of up to `N - 1` minutes of data loss on a power cut. File is always flushed at day rollover and before cleanup. See [SD Output](/output/sdcard). |

## Display Settings (OLED builds)

| Setting | Type | Default | Description |
|---|---|---|---|
| Brightness | Int 0-100 | `25` | Display brightness |
| Timeout | Int 0-99999 | `120` | Display timeout in seconds, 0=off (push button builds only) |
| On Time | Time HH:MM | `06:00` | Display on-time (builds without push button) |
| Off Time | Time HH:MM | `22:00` | Display off-time (builds without push button) |

## NeoPixel Settings (NeoPixel builds)

| Setting | Type | Default | Description |
|---|---|---|---|
| Brightness | Int 0-100 | `15` | NeoPixel brightness, 0 disables. Too low a value can cause inaccurate colours. |
