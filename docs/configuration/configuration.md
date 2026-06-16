---
layout: page
title: Configuration
permalink: /configuration
has_children: true
nav_order: 4
---

# Configuration

Most settings are available from the ESPGeiger [Web Portal](/output/webportal) - click **Config** on the main page.

Specialised pages with their own UI:

* **NTP Config** - time zone and NTP server. See [NTP Configuration](/configuration/ntp).
* **HV Config** - high-voltage tuning for ESPGeiger-HW boards. See [HV Configuration](/configuration/hv).

Settings changed on the Config page take effect immediately (or on next submission cycle for output modules). Pin and serial type changes require a reboot.

The Config page is split across five tabs: **System**, **Input**, **Output**, **Upload** and **Backup**.

## System Settings

| Setting | Type | Default | Description |
|---|---|---|---|
| Friendly name | String (32) | `(empty)` | Optional human label shown in the web UI title and `/about`. Blank falls back to the hostname (e.g. `ESPGeiger-a1b2c3`). |
| uSv/h Ratio | Float | `151.0` | CPM to uSv/h conversion factor |
| Warning CPM | Int 0-9999 | `50` | CPM threshold for warning state |
| Alert CPM | Int 0-9999 | `100` | CPM threshold for alert state |
| Web password | String (32) | `(empty)` | Optional HTTP Basic Auth password (user is `admin`). Blank disables auth. Sensitive. |
| Track lifetime | Boolean | `true` | Persist total clicks + first-boot timestamp + tracked seconds across reboots. Tracked seconds advance only over spans where clicks were persisted, so the lifetime CPM on `/hist` stays accurate across crashes. Toggling off pauses the counter without losing the saved value. View totals and reset from `/hist`. |

### Config import / export

The **Backup** tab has a single textarea labelled **Backup / Restore**. Click **Export** to dump the entire prefs blob (skipping `sys.web_pass` and the whole `net` group as device-local) as base64 with a CRC32 trailer; copy or paste a previous export and click **Import** to restore. Import is overlay-only: keys in the blob overwrite existing values; everything else is left alone. The device reboots automatically on a successful import.

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
| Debounce (us) | Int 0-10000 | `200` | Software interrupt debounce. Pulse builds without PCNT only. |
| Tube dead time | Int 0-1000 | `0` | GM tube dead time in microseconds, used to compensate the rate at high CPS. Pulse builds only. `0` disables. Typical values: J305 = 50, SBM-20 = 150. |
| α Alpha / β Beta / γ Gamma | Boolean | `false` | Which radiation types the connected tube detects. Surfaces alongside the **Geiger Counter** model field. Informational; consumed by upstream aggregators. Hidden on tests builds and when `-D GEIGER_MODEL_FIXED`. |
| Pulse width (us) | Int 10-2000 | `(varies)` | Width of the simulated pulse on the TX pin. Test builds only (`test`, `testpulse`). |

## Blip LED Settings

| Setting | Type | Default | Description |
|---|---|---|---|
| Blip LED | Boolean | `true` | Flash the onboard LED on each detected count. Hidden on builds with `-D DISABLE_INTERNAL_BLIP`. |
| Blip brightness | Int 0-100 | `80` | Onboard LED brightness as a percentage (PWM duty). Also applies to network-activity blinks (MQTT/WebAPI/etc.). Hidden on builds with `-D DISABLE_INTERNAL_BLIP`. |
| Quiet from | Time HH:MM | `(empty)` | Silence the blip LED + beeper (ESPGeiger-HW) from this time. Leave blank to disable quiet hours. Requires NTP sync; if time isn't known, quiet hours are ignored. |
| Quiet to | Time HH:MM | `(empty)` | End of quiet window. Window crosses midnight if `from > to` (e.g. `22:00`-`07:00`). |

## UDP Receiver Input (UDP-Receiver builds)

UDP-Receiver builds (`*_udp` targets) listen for [Local Broadcast](/output/udp) traffic from another ESPGeiger and feed the received clicks into the same Counter pipeline as a real tube. These prefs replace the Pulse / Serial input fields above.

| Setting | Type | Default | Description |
|---|---|---|---|
| Source Mode | Int 0-1 | `0` | `0` = specific producer (auto-latches the first one heard if **Pin chipid** is blank, otherwise locks to the chipid given). `1` = sum mode, totalises clicks from every producer heard (up to 8). |
| Pin chipid | String (6 hex) | `(empty)` | 6-hex chipid of the producer to lock to. Only used in Source Mode `0`. Blank = auto-latch. |
| Multicast group | String (15) | `239.255.86.86` | Multicast IP to subscribe to. Must match the producer's group. |
| Port | Int 1-65535 | `57340` | UDP port. Must match the producer. |
| RX sleep | Int 0-2 | `1` | WiFi sleep mode. `0` = light (DTIM-aligned wake + CPU nap, lowest power), `1` = modem (DTIM wake + CPU awake, balanced default), `2` = none (radio always on; usually higher loss on busy LANs, not lower). See [RX Sleep Mode](/output/udp#rx-sleep-mode-udprx_rxmode). |

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

## Advanced Wi-Fi Tunables

These do not appear on the Config page. The defaults suit almost every deployment; the table is here for the rare case where you need to override one (for example a router on a channel disabled by the device's default country code, or to reduce TX power on a battery build).

Set a value via the device's `/pref` URL:

```
http://<device>.local/pref?group=wifi&key=<key>&value=<value>
```

Changes take effect on the next reboot.

| Setting | Type | Default | Description |
|---|---|---|---|
| `sleep` | Int 0-2 | `1` ESP8266 / `2` ESP32 | Wi-Fi radio power save. `0` = light sleep (lowest power, ping varies the most), `1` = modem sleep (DTIM aligned, balanced), `2` = none (radio always on, snappiest ping, around 30 mA more current). USB-powered ESP32 devices benefit from `2`; battery builds usually prefer `1`. UDP-Receiver builds use the **RX sleep** setting on the Input tab instead of this one. |
| `tx_power` | Int 0-20 | `0` | Transmit power in dBm. `0` keeps the platform default (typically around 20 dBm). Lower values can reduce coupling with sensitive analogue circuitry on the same board and save power on battery builds, at the cost of effective range. |
| `country` | String (2) | `(empty)` | ISO 2-letter country code (for example `GB`, `US`, `DE`, `JP`). Empty uses the platform default, which restricts channels in some regions. Set this if your router is on channel 12 or 13 (EU) or 14 (JP) and the device cannot see or connect to it. |
| `phy_mode` | Int 0-3 | `0` | Wi-Fi PHY lock-down. `0` lets the device negotiate (recommended). `1` = 802.11b only, `2` = b/g, `3` = b/g/n. Use this only as a workaround for routers that fail to negotiate higher rates cleanly. |
