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

## Input Settings

Some settings only appear on specific builds. Pin and Serial Type changes trigger a reboot to apply; PCNT Filter, PCNT Pin Pull and Debounce apply live.

| Setting | Type | Default | Description |
|---|---|---|---|
| Serial Type | Int 1-255 | `(varies)` | Serial counter protocol: 1=GC10, 2=GC10Next, 3=MightyOhm, 4=ESPGeiger. Only shown on serial builds. Reboot required. |
| CPM Window (CPM-only counters) | Int 1-60 | `30` | Serial builds only, applies to CPM-only counters (GC10, GC10Next, ESPGeiger-serial). Rolling CPM window in seconds; lower values track the counter's display more closely during rate changes, higher values smooth out natural variance. Ignored for MightyOhm, which uses a 60s window since its CPS feed is already exact. Reboot required. See [Serial Counters](/hardware/serial#cpm-window-option). |
| RX Pin | Int | `(varies)` | Geiger counter input pin. Hidden when `-D RXPIN_BLOCKED`. Reboot required. |
| TX Pin | Int | `(varies)` | Transmit pin. Only on builds with a TX pin configured. Reboot required. |
| Geiger Counter | String (32) | `(varies)` | GM tube model name (e.g. SBM-20, J305). Pulse builds only. Hidden when `-D GEIGER_MODEL_FIXED` (purpose-built kits where the tube is fixed in firmware). |
| PCNT Filter | Int 0-1023 | `200` | Glitch filter threshold (ESP32 PCNT builds only). `0` disables. See [PCNT Filter](/hardware/esphardware#pcnt-filter). |
| PCNT Pin Pull | Int 0-2 | `0` | PCNT input pin pull: `0`=none (floating), `1`=up, `2`=down. ESP32 PCNT builds only. Default floating suits most modules that drive the line actively both directions; change to pull-up for open-drain outputs or pull-down for active-high modules without their own idle pull. |
| Debounce (us) | Int 0-10000 | `500` | Software interrupt debounce. Pulse builds without PCNT only. |
| Blip LED | Boolean | `true` | Flash the onboard LED on each detected count. Hidden on builds with `-D DISABLE_INTERNAL_BLIP`. |
| Blip brightness | Int 0-100 | `80` | Onboard LED brightness as a percentage (PWM duty). Also applies to network-activity blinks (MQTT/WebAPI/etc.). Hidden on builds with `-D DISABLE_INTERNAL_BLIP`. |
| Quiet from | Time HH:MM | `(empty)` | Silence the blip LED + beeper (ESPGeiger-HW) from this time. Leave blank to disable quiet hours. Requires NTP sync; if time isn't known, quiet hours are ignored. |
| Quiet to | Time HH:MM | `(empty)` | End of quiet window. Window crosses midnight if `from > to` (e.g. `22:00`-`07:00`). |

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
| Timeout | Int 0-99999 | `120` | Push-button builds only. Display turns off after this many seconds of inactivity. `0` falls back to On Time / Off Time below. |
| On Time | Time HH:MM | `06:00` | Display on-time. On push-button builds, only takes effect when Timeout is `0`. Blank means always on. |
| Off Time | Time HH:MM | `22:00` | Display off-time. Same rules as On Time. Window crosses midnight if `from > to`. |
| I2C SDA Pin | Int 0-39 | `(varies)` | OLED SDA pin. Hidden when `-D OLED_PINS_BLOCKED`. Reboot required. |
| I2C SCL Pin | Int 0-39 | `(varies)` | OLED SCL pin. Hidden when `-D OLED_PINS_BLOCKED`. Reboot required. |

On push-button builds the long-press gesture toggles a permanent override that keeps the display on regardless of Timeout or schedule. Re-toggle to restore the configured behaviour.

## NeoPixel Settings (NeoPixel builds)

| Setting | Type | Default | Description |
|---|---|---|---|
| Brightness | Int 0-100 | `15` | NeoPixel brightness, 0 disables. Too low a value can cause inaccurate colours. |
