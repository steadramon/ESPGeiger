---
layout: page
title: Environment Sensors
permalink: /hardware/env
parent: Hardware
nav_order: 6
---

# Environment Sensors

ESPGeiger can read temperature, humidity, and atmospheric pressure from a cheap I2C sensor on the same bus as the OLED. Detection is automatic at boot. Add a sensor and the values appear on `/status` and relevant outputs.

For wiring, pins, and per-output details see the [Environment sensor configuration page](/configuration/env).

## Which one to buy

| Module | What you get | Notes |
|---|---|---|
| **[AHT20 + BMP280 combo](https://s.click.aliexpress.com/e/_c3MHXRZb)** | T + H + P | Two chips on one board (AHT20 at 0x38, BMP280 at 0x76). The cheapest way to get all three readings. Often sold as a "BME280 replacement". |
| **[BME280](https://s.click.aliexpress.com/e/_c4m8Agg5)** | T + H + P | Single-chip Bosch part, ±0.3 °C and ±3 % RH. Buy from a seller you trust - BMP280s (no humidity) are routinely re-labelled as BME280 because the chip packages look almost identical. |

Either is fine for ESPGeiger's purposes - the combo board is usually cheaper and ships in a day.

## The BMP280-pretending-to-be-BME280 trap

If you bought a "BME280" and the humidity row is missing on `/status`, you almost certainly got a BMP280. The firmware reports what it found at boot as `Env: <chip> detected` - check the serial console, or the Console panel on the `/status` page. There's no firmware workaround - the chip physically doesn't have a humidity sensor. Either return it or accept temperature + pressure only.

## AHT-only modules

Bare AHT10/15/20/21/25/30 modules (no BMP280) work too - you get temperature and humidity but no pressure. Worth it only if you already have one in a drawer.

## Connection

Four pins: VCC (3.3 V), GND, SDA, SCL. Shares the I2C bus with the OLED if one is fitted. Default pins per platform are listed on the [configuration page](/configuration/env).
