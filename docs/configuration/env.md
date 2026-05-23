---
layout: page
title: Environment sensor
permalink: /configuration/env
nav_order: 30
parent: Configuration
---

# Environment sensor

ESPGeiger can read temperature, humidity, and atmospheric pressure from a supported I2C sensor wired to the device. Detection is automatic at boot - no build flag, no firmware variant. If a supported sensor is found, the values appear on the `/status` page below the radiation readings and on every output channel (MQTT, Webhook, WebAPI, UDP, Serial templates, Thingspeak).

If no sensor is wired in, the firmware uses zero memory beyond the small probe at boot.

## Supported sensors

| Chip | I2C address | Provides | Notes |
|---|---|---|---|
| BME280 | 0x76 / 0x77 | T + H + P | Most common, ±0.3 °C, ±3 % RH |
| BMP280 | 0x76 / 0x77 | T + P | No humidity, often mis-sold as BME280 |
| AHT10 / AHT15 | 0x38 | T + H | Older Asair part |
| AHT20 / AHT21 | 0x38 | T + H | The dominant AHT module today |
| AHT25 | 0x38 | T + H | Precision-binned, ±0.2 °C |
| AHT30 | 0x38 | T + H | Wider temperature range (-40 to 120 °C) |
| AHT20 + BMP280 (combo) | 0x38 + 0x76 | T + H + P | Sold as "BME280 replacement" - both chips on one board |

Other Bosch parts (BMP180, BMP380, BME680) and other vendor parts (SHT4x, HTU21D, DPS310, etc.) are not currently supported.

## Wiring

Connect SDA, SCL, VCC (3.3 V), GND. Default pins:

| Platform | SDA | SCL |
|---|---|---|
| ESP8266 | 4 | 5 |
| ESP32 (classic) | 21 | 22 |
| ESP32-S2 / S3 | 8 | 9 |
| ESP32-C3 / C6 | 6 | 7 |

If your build also has an OLED, the sensor must share the same I2C pins as the OLED. ESPGeiger uses a single I2C bus.

If you need different pins, change them in **Configuration → Environment**. A reboot is required to apply pin changes.

## Settings

| Setting | Default | Range |
|---|---|---|
| `env.sda` | platform default | 0 - MAX_GPIO |
| `env.scl` | platform default | 0 - MAX_GPIO |
| `env.unit` | 0 (°C) | 0=°C, 1=°F, 2=K |

`env.unit` only affects what is shown on the local `/status` page and Serial templates. All outputs (MQTT, Webhook, WebAPI, UDP, Thingspeak) always publish in canonical units (°C, %RH, hPa) so consumers can convert if they want.

## Combo modules

The popular AliExpress "AHT20+BMP280" module has two chips on one board (AHT20 at 0x38, BMP280 at 0x76). ESPGeiger detects both, composes temperature and humidity from the AHT20 (higher precision), and pressure from the BMP280. The chip name in `/info` will show `AHTxx+BMP280`. The BMP280's own temperature reading is used internally for pressure compensation but not exposed.

## Output channels

When a sensor is present, the firmware adds the relevant fields to each output channel. Each field is sent independently, so a BMP280 ships temp + pressure but not humidity, an AHT alone ships temp + humidity but not pressure.

* **`/status` page**: a single "Env" row appears under the radiation readings, e.g. `22.4°C  45%  1013 hPa`. Updates live with the rest of the status polling.
* **MQTT**: `tele/sensor` JSON gains `t` / `h` / `p`, and the legacy topics `stat/T` / `stat/H` / `stat/P`. Home Assistant autodiscovery publishes one sensor entity per available field.
* **Webhook**: JSON body gains `t` / `h` / `p`.
* **WebAPI**: MsgPack payload gains `t` / `h` / `p`.
* **UDP / OSC**: new `/espg/<id>/env` schema `,fff` (temp, humidity, pressure) every 60 s.
* **Thingspeak**: maps temp / humidity / pressure to `field5` / `field6` / `field7`. Configure those fields in your Thingspeak channel UI.
* **Serial template** (`sout.format=5`): variables `{t}` `{h}` `{p}` available for substitution.

Historical trends are not stored on the device - if you want a 24-hour chart, point your MQTT broker at Home Assistant, Grafana, or any other dashboard tool.

## Diagnostics

Visit `/vars.json` on the device to see the current rendered value of every variable, including environmental ones. Useful when authoring Serial templates or debugging a missing humidity reading.

## Troubleshooting

* **Env row missing on `/status`.** No sensor was found at boot. Check 3.3 V supply, pull-up resistors (4.7 kΩ typical), SDA/SCL wiring, and the `env.sda` / `env.scl` pref values. Enable debug logging to see the probe attempt in the console.
* **Humidity missing from the row.** You probably have a BMP280, not a BME280. The two parts look similar on a board but only BME280 has the humidity sensor. The chip name on `/info` will tell you which one you have.
* **Pressure missing.** You probably have an AHT-only module without the paired BMP280. Add a BMP280 on the same I2C bus, or accept that pressure is not available.
* **Wrong temperature just after boot.** The first sample seeds the EMA. Allow 30 seconds for the smoothed value to settle.
