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

* **Network Config** - Wi-Fi, static IP, time zone and NTP server. See [Time and NTP](/configuration/ntp).
* **HV Config** - high-voltage tuning for ESPGeiger-HW boards. See [HV Configuration](/configuration/hv).

Settings changed on the Config page take effect immediately (or on next submission cycle for output modules). Pin and serial type changes require a reboot.

The Config page is split across five tabs: **System**, **Input**, **Output**, **Upload** and **Backup**.

## System Settings

<!-- gen:prefs sys -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Friendly name | `sys.name` | String (32) | (empty) | Optional human label shown in the web UI title and `/about`. Blank falls back to the hostname (e.g. `ESPGeiger-a1b2c3`). |
| Web password | `sys.web_pass` | String (32) | (empty) | Optional HTTP Basic Auth password (user is `admin`). Blank disables auth. Sensitive. |

<!-- /gen -->

### Config import / export

On the **Backup** tab, **Export** copies all settings except the web password and network settings into the text box. Paste a saved export and click **Import** to restore it; settings not in the export are left alone, and the device reboots.

## Counter Settings

On the **Input** tab. Some settings only appear on specific builds. Pin and Serial Type changes trigger a reboot to apply; PCNT Filter, PCNT Pin Pull and Debounce apply live.

<!-- gen:prefs counter skip=udprx_mode,udprx_chipid,udprx_group,udprx_port,udprx_rxmode -->

| Setting | Key | Type | Default | Builds | Description |
|---|---|---|---|---|---|
| Serial Type | `counter.serial_type` | `1` GC10, `2` GC10Next, `3` MightyOhm, `4` ESPGeiger, `5` Template | `1` GC10 | serial builds | Serial counter protocol, see [Serial Counters](/hardware/serial). Reboot required. |
| RX Pin | `counter.rx_pin` | Int 0-max GPIO | varies by build | boards without fixed pins | Geiger counter input pin. Reboot required. |
| TX Pin | `counter.tx_pin` | Int 0-max GPIO | varies by build | builds with a TX pin | Transmit pin. Reboot required. |
| PCNT Filter | `counter.pcnt_filter` | Int 0-1023 | `200` | ESP32 PCNT builds | Glitch filter threshold. `0` disables. See [PCNT Filter](/hardware/esphardware#pcnt-filter). |
| PCNT Pull | `counter.pcnt_pull` | `0` None, `1` Pull-up, `2` Pull-down | `0` None, unless the board sets a pull | ESP32 PCNT builds | PCNT input pin pull. Floating suits most modules that drive the line actively both directions; change to pull-up for open-drain outputs or pull-down for active-high modules without their own idle pull. |
| Debounce | `counter.debounce` | Int 0-10000 | `200` | pulse builds without PCNT | Software interrupt debounce in microseconds. Filters electrical noise; 100-300 is typical. |
| Pulse width | `counter.pulse_width_us` | Int 10-2000 | `500` | `test`, `testpulse` | Width of the simulated pulse on the TX pin, in microseconds. |
| CPM Window (CPM-only counters) | `counter.cpm_window` | Int 1-60 | `30` | serial builds | Applies to CPM-only counters (GC10, GC10Next, ESPGeiger-serial). Rolling CPM window in seconds; lower values track the counter's display more closely during rate changes, higher values smooth out natural variance. Ignored for MightyOhm, which uses a 60s window since its CPS feed is already exact. Reboot required. See [Serial Counters](/hardware/serial#cpm-window-option). |
| CPM mode | `counter.cpm_mode` | `0` Fast start, `1` Live ring, `2` Fixed 60s, `3` Bucket, `4` Adaptive fast | `3` Bucket | all | How the 1-minute CPM is computed. See [CPM Calculation Mode](/configuration/cpm-mode). |
| Geiger Counter | `counter.geiger_model` | String (32) | varies by build | not test builds | GM tube model name (e.g. SBM-20, J305). Hidden when `-D GEIGER_MODEL_FIXED` (purpose-built kits where the tube is fixed in firmware). |
| α Alpha | `counter.tube_alpha` | Boolean | `0` off | not test builds | The tube detects alpha radiation. Informational; passed to upstream aggregators. |
| β Beta | `counter.tube_beta` | Boolean | `0` off | not test builds | The tube detects beta radiation. Informational. |
| γ Gamma | `counter.tube_gamma` | Boolean | `0` off | not test builds | The tube detects gamma radiation. Informational. |
| Tube dead time | `counter.dead_time_us` | Int 0-1000 | `100` (`0` on test builds) | pulse builds | GM tube dead time in microseconds, used to compensate the rate at high CPS. `0` disables. Typical values: J305 = 50, SBM-20 = 150. |
| uSv/h Ratio | `counter.ratio` | Float 1-100000 | `151.0` | all | CPM to uSv/h conversion factor |
| Warning CPM | `counter.warn` | Int 0-9999 | `50` | all | CPM threshold for warning state |
| Alert CPM | `counter.alert` | Int 0-9999 | `100` | all | CPM threshold for alert state |
| Track lifetime | `counter.life_en` | Boolean | `1` on | all | Persist total clicks + first-boot timestamp + tracked seconds across reboots. Tracked seconds advance only over spans where clicks were persisted, so the lifetime CPM on `/hist` stays accurate across crashes. Toggling off pauses the counter without losing the saved value. View totals and reset from `/hist`. |

<!-- /gen -->

## Blip LED Settings

<!-- gen:prefs led -->

| Setting | Key | Type | Default | Builds | Description |
|---|---|---|---|---|---|
| Blip LED | `led.blip_led` | Boolean | `1` on | all | Flash the onboard LED on each detected count. Hidden on builds with `-D DISABLE_INTERNAL_BLIP`. |
| Mode | `led.mode` | `0` Pulse, `1` Fade | `0` Pulse | all | `Pulse` switches the LED off after the pulse width; `Fade` ramps it down. |
| Pulse width ms | `led.pulse_ms` | Int 1-50 | `20` (`2` on ESPGeiger-HW) | all | How long the LED stays lit per count, in milliseconds. |
| Fade rate | `led.fade_shift` | `0` Fast, `1` Medium, `2` Slow | `1` Medium | all | Fade mode only. |
| Blip brightness | `led.blip_bright` | Int 0-100 | `80` (`100` on ESPGeiger-HW) | all | Onboard LED brightness as a percentage (PWM duty). Also applies to network-activity blinks (MQTT/WebAPI/etc.). |
| Quiet from | `led.quiet_from` | Time HH:MM | (empty) | not ESPGeiger-HW | Silence the blip LED + beeper from this time. Leave blank to disable quiet hours. Requires NTP sync; if time isn't known, quiet hours are ignored. |
| Quiet to | `led.quiet_to` | Time HH:MM | (empty) | not ESPGeiger-HW | End of quiet window. Window crosses midnight if `from > to` (e.g. `22:00`-`07:00`). |
| Status LED | `led.pin` | Int -1 to max GPIO | varies by build | ESP32 | GPIO of the board's own LED, `-1` = none. Defaults to `2` on ESP32 and `-1` on C3/S3, which have no plain LED on GPIO 2. ESP8266 is fixed at GPIO 2. A pin another setting already uses stays with that setting and the LED stays off. Reboot to apply. |
| Polarity | `led.polarity` | `0` Active high, `1` Active low | varies by build | ESP32 | `Active low` for an LED that lights when its pin is low, such as the C3 SuperMini (GPIO 8) or Lolin32 Lite (GPIO 22). Reboot to apply. |

<!-- /gen -->

## UDP Receiver Input (UDP-Receiver builds)

UDP-Receiver builds (`*_udp` targets) listen for [Local Broadcast](/output/udp) traffic from another ESPGeiger and feed the received clicks into the same Counter pipeline as a real tube. These prefs replace the Pulse / Serial input fields above.

<!-- gen:prefs counter only=udprx_mode,udprx_chipid,udprx_group,udprx_port,udprx_rxmode -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Source Mode | `counter.udprx_mode` | `0` Specific device, `1` Sum all sources | `0` Specific device | `0` = specific producer (auto-latches the first one heard if **Pin chipid** is blank, otherwise locks to the chipid given). `1` = sum mode, totalises clicks from every producer heard (up to 8). |
| Pin chipid | `counter.udprx_chipid` | String (6) | (empty) | 6-hex chipid of the producer to lock to. Only used in Source Mode `0`. Blank = auto-latch. |
| Group / listen IP | `counter.udprx_group` | String (15) | `239.255.86.86` | 239.x = multicast, else unicast listen. |
| Port | `counter.udprx_port` | Int 1-65535 | `57340` | UDP port. Must match the producer. |
| RX sleep | `counter.udprx_rxmode` | `0` Light (low power), `1` Modem (balanced), `2` None (always on) | varies by build | WiFi sleep while receiving. Modem suits most networks; the original ESP32 defaults to None. See [Reliability and tuning](/output/udp#reliability-and-tuning). |

<!-- /gen -->

## MQTT Configuration

<!-- gen:prefs mqtt -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Server | `mqtt.server` | String (64) | (empty) | MQTT broker address or IP. Leave empty to disable MQTT. |
| Port | `mqtt.port` | Int 1-65535 | `1883` | MQTT broker port |
| User | `mqtt.user` | String (64) | (empty) | MQTT username |
| Password | `mqtt.password` | String (64) | (empty) | MQTT password (sensitive) |
| Root Topic | `mqtt.topic` | String (28) | `ESPGeiger-{id}` | Root publish topic. `{id}` is replaced with the chip ID. |
| Interval | `mqtt.interval` | Int 5-3600 | `60` | Publish interval in seconds |
| HA Autodiscovery | `mqtt.hass_enabled` | Boolean | `1` on (`0` on test builds) | Publish Home Assistant autodiscovery on connect |
| HA Autodiscovery Topic | `mqtt.hass_topic` | String (32) | `homeassistant` | HA discovery prefix |

<!-- /gen -->

## Radmon.org Configuration

<!-- gen:prefs radmon -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Enable | `radmon.send` | Boolean | `0` off | Upload to radmon.org |
| Username | `radmon.user` | String (32) | (empty) | Radmon.org username |
| Password | `radmon.password` | String (64) | (empty) | Radmon.org password (sensitive) |
| Interval | `radmon.interval` | Int 30-1800 | `60` | Upload interval in seconds |

<!-- /gen -->

## ThingSpeak Configuration

<!-- gen:prefs thingspeak -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Enable | `thingspeak.send` | Boolean | `0` off | Upload to ThingSpeak |
| Channel Key | `thingspeak.channel_key` | String (16) | (empty) | ThingSpeak channel write API key (sensitive) |

<!-- /gen -->

## GMC Configuration

<!-- gen:prefs gmc -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Enable | `gmc.send` | Boolean | `0` off | Upload to gmcmap.com |
| Account ID | `gmc.aid` | String (20) | (empty) | gmcmap.com account ID (numeric) |
| Geiger Counter ID | `gmc.gcid` | String (20) | (empty) | gmcmap.com geiger counter ID (numeric) |

<!-- /gen -->

## Webhook Configuration

<!-- gen:prefs webhook -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Enable | `webhook.send` | Boolean | `0` off | POST measurements to a webhook |
| URL | `webhook.url` | String (255) | (empty) | Webhook endpoint (http only) |
| Key | `webhook.key` | String (255) | (empty) | Optional shared secret sent as "key" in payload (sensitive) |
| Interval | `webhook.interval` | Int 10-3600 | `60` | POST interval in seconds |

<!-- /gen -->

## SD Card Settings (SD-enabled builds)

<!-- gen:prefs sdcard -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Sync Interval (min) | `sdcard.sync_min` | Int 1-5 | `1` | Minutes between syncs to the card. `1` syncs on every write (safest). Higher values reduce SD wear at the cost of up to `N - 1` minutes of data loss on a power cut. File is always flushed at day rollover and before cleanup. See [SD Output](/output/sdcard). |

<!-- /gen -->

## I2C Bus

One bus for the OLED and the [environment sensor](/configuration/env). Upgrades carry the pins over from the old `display.sda`/`scl` or `env.sda`/`scl`.

<!-- gen:prefs i2c -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| SDA Pin | `i2c.sda` | Int 0-max GPIO | varies by build | Reboot to apply |
| SCL Pin | `i2c.scl` | Int 0-max GPIO | varies by build | Reboot to apply |

<!-- /gen -->

## Display Settings (OLED builds)

<!-- gen:prefs display -->

| Setting | Key | Type | Default | Builds | Description |
|---|---|---|---|---|---|
| Display Type | `display.type` | `0` SSD1306, `1` SH1106 (1.3in), `2` SSD1309 | varies by build | boards without fixed pins | OLED panel, or an LED display on builds with MAX7219 support: `3` Tiny SSD1306 and `4` Tiny SH1106 (ESP32), `5` 7-Seg (MAX7219), `6` Round (Mottram MAX7219), `7` Matrix (MAX7219), `8` 7-Seg (TM1637). Reboot to apply. |
| Brightness | `display.brightness` | Int 0-100 | `25` | all | Display brightness |
| Reset Pin | `display.rst` | Int 0-255 | varies by build | boards without fixed pins | GPIO toggled at boot to reset OLED (255 = none). Reboot to apply. |
| Flip 180° | `display.flip` | Boolean | varies by build | boards without fixed pins | Reboot to apply |
| Timeout | `display.timeout` | Int 0-99999 | `120` | push-button builds | Display turns off after this many seconds of inactivity. `0` falls back to On Time / Off Time below. |
| On Time | `display.on_time` | Time HH:MM | (empty) | all | Display on-time. On push-button builds, only takes effect when Timeout is `0`. Blank means always on. |
| Off Time | `display.off_time` | Time HH:MM | (empty) | all | Display off-time. Same rules as On Time. Window crosses midnight if `from > to`. |

<!-- /gen -->

On push-button builds the long-press gesture toggles a permanent override that keeps the display on regardless of Timeout or schedule. Re-toggle to restore the configured behaviour.

### MAX7219

Shown while Display Type is a MAX7219 layout: `7-Seg` (up to 8 digits), `Round` (4 digits and a 24-LED arc) or `Matrix` (1 to 8 chained 8x8 panels), or a `7-Seg (TM1637)` module. Pages step through CPM, µSv/h, a CPM graph on the matrix and, with an environment sensor, temperature and humidity. The Round starts on a VU meter page before those.

The TM1637 has two wires: its DIO goes on the Data Pin and its CLK on the CLK Pin. CS is not used.

The `Round` arc (MottramLabs MAX7219 Power Display) lights green up to the System **warn** CPM, orange up to **alert**, and red up to twice **alert**. On the VU page the digits hold CPM and each count kicks the arc, which falls back like a needle and auto-ranges so the usual rate bobs around the green/orange border. The board is built for 5V logic: from an ESP, power it from 3V3 or put a level shifter on DIN, CLK and CS.

<!-- gen:prefs max7219 -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| Data Pin (DIN/DIO) | `max7219.din` | Int -1 to max GPIO | `-1` | Reboot to apply |
| CLK Pin | `max7219.clk` | Int -1 to max GPIO | `-1` | Reboot to apply |
| CS Pin | `max7219.cs` | Int -1 to max GPIO | `-1` | Reboot to apply |
| Pages | `max7219.mode` | `0` Cycle, `1` Fixed | `1` Fixed | Cycle auto-advances; Fixed holds until the button is pressed |
| Digits | `max7219.count` | Int 1-8 | `8` | 7-seg digit count. TM1637 takes 2-6, anything over 6 shows as 4 |
| Panels | `max7219.panels` | Int 1-8 | `4` | 8x8 panels in the chain |
| uSv decimal places | `max7219.decimals` | Int 0-3 | `2` | Decimal places on the µSv/h page. |
| Level bar (decimal-point row) | `max7219.bar` | Boolean | `1` on | Light the decimal points as a level bar (7-seg, CPM only) |
| Bar sensitivity | `max7219.bar_gain` | Int 1-10 | `2` | Bar fills at this multiple of the recent average |
| Bar baseline | `max7219.bar_win` | `0` 5 min, `1` 15 min | `1` 15 min | The recent average the bar scales against. |
| Matrix module | `max7219.orient` | `0` FC-16, `1` Generic, `2` FC-16 upside down, `3` Generic upside down | `0` FC-16 | Matrix layout only. FC-16 is the common 4-in-1 strip; Generic the single 8x8 kit with the chip beside the matrix (untested on hardware). Upside down also runs the chain backwards. If text is garbled or mirrored, try the others. |
| Reverse digits | `max7219.reverse` | Boolean | `0` off | 7-seg: for modules that show 56781234 |
| TM1637 points | `max7219.points` | `0` Dots, `1` Colon only | `0` Dots | Colon only: page letters (C U t H), the colon is the uSv point |

<!-- /gen -->

## NeoPixel Settings (NeoPixel builds)

<!-- gen:prefs neopixel -->

| Setting | Key | Type | Default | Builds | Description |
|---|---|---|---|---|---|
| Pin | `neopixel.pin` | Int -1 to max GPIO | varies by build | boards without fixed pins | WS2812 data pin. `-1` leaves the module inert (no LED wired). Set to the GPIO your pixel is on. Reboot to apply. |
| Brightness | `neopixel.brightness` | Int 0-100 | `15` | all | NeoPixel brightness, 0 disables and unloads the module. Too low a value can cause inaccurate colours. |
| Mode | `neopixel.mode` | `0` Off, `1` Blip, `2` Blip+status, `3` Trend pulse, `4` Trend+status | `4` Trend+status | all | Render mode. See [NeoPixel output](/output/neopixel) for behaviour. |
| Blip colour | `neopixel.color` | `0` Green, `1` Red, `2` Blue, `3` Yellow, `4` Cyan, `5` Magenta, `6` White, `7` Orange | `0` Green | all | Colour for mode 1 only. |
| Fade rate | `neopixel.fade` | `0` Off, `1` Fast, `2` Medium, `3` Slow | `0` Off | all | Blip mode only. |
| Swap R/G | `neopixel.swap` | Boolean | varies by build | all | Enable if green appears as red on RGB-ordered LED chips. |

<!-- /gen -->

## Alert Out Settings

Drives a GPIO when CPM crosses the alert threshold. See [Alert Out](/output/alertout) for wiring options.

<!-- gen:prefs alert -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| GPIO | `alert.pin` | Int -1 to max GPIO | `-1` | Output pin. `-1` disables the module. Reboot to apply. |
| Polarity | `alert.polarity` | `0` Active high, `1` Active low | `0` Active high | Output level while alerting. |
| Mode | `alert.mode` | `0` Steady level, `1` 1 Hz pulse | `0` Steady level | Hold the level while alerting, or toggle it as a 1 Hz square wave. |

<!-- /gen -->

## Advanced Wi-Fi Tunables

These do not appear on the Config page. The defaults suit almost every deployment; the table is here for the rare case where you need to override one (for example a router on a channel disabled by the device's default country code, or to reduce TX power on a battery build).

Set a value via the command box on the Status page (or over USB serial at 115200 baud) using the `set <module>.<key> <value>` form:

```
set wifi.tx_power 8
set wifi.country GB
reboot
```

Changes take effect on the next reboot.

<!-- gen:prefs wifi hidden -->

| Setting | Key | Type | Default | Description |
|---|---|---|---|---|
| `sleep` | `wifi.sleep` | Int 0-2 | `1` | Wi-Fi radio power save. `0` = light sleep (lowest power, ping varies the most), `1` = modem sleep (DTIM aligned, balanced), `2` = none (radio always on, snappiest ping, around 30 mA more current). USB-powered ESP32 devices benefit from `2`; battery builds usually prefer `1`. UDP-Receiver builds use the **RX sleep** setting on the Input tab instead of this one. |
| `tx_power` | `wifi.tx_power` | Int 0-20 | `0` | Transmit power in dBm. `0` keeps the platform default (typically around 20 dBm). Lower values can reduce coupling with sensitive analogue circuitry on the same board and save power on battery builds, at the cost of effective range. |
| `country` | `wifi.country` | String (3) | (empty) | ISO 2-letter country code (for example `GB`, `US`, `DE`, `JP`). Empty uses the platform default, which restricts channels in some regions. Set this if your router is on channel 12 or 13 (EU) or 14 (JP) and the device cannot see or connect to it. |
| `phy_mode` | `wifi.phy_mode` | Int 0-3 | `0` | Wi-Fi PHY lock-down. `0` lets the device negotiate (recommended). `1` = 802.11b only, `2` = b/g, `3` = b/g/n. Use this only as a workaround for routers that fail to negotiate higher rates cleanly. |

<!-- /gen -->

## CPU Frequency (ESP32 only)

Hidden `sys.cpu_mhz` pref forces the CPU clock. `0` (default) leaves the board's default in place; `80`, `160` or `240` override (ESP32-C3 silicon caps at 160 MHz, so 240 is silently ignored there). Lowering the clock noticeably reduces power draw and die temperature on hot-running boards such as the ESP32-C3 Zero, at the cost of LPS headroom. The setting is per-device and is not included in config export blobs.

Set via the Status-page command box or USB serial:

```
set sys.cpu_mhz 80
reboot
```
