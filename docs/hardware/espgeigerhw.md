---
layout: page
title: ESPGeigerHW
permalink: /hardware/espgeigerhw
parent: Hardware
nav_order: 3
---
<style>
#espghwimg {
  width: 100%;
  max-height: 300px;
}
</style>
# ESPGeigerHW
{: .no_toc }

<details open markdown="block">
  <summary>
    Table of contents
  </summary>
  {: .text-delta }
- TOC
{:toc}
</details>

---

<img id="espghwimg" src="../img/ESPGeiger-HW-v4.svg" alt="ESPGeigerHW board">
{: .text-center }

<a href="https://www.tindie.com/stores/espgeiger/?ref=offsite_badges&utm_source=sellers_paulstead&utm_medium=badges&utm_campaign=badge_medium"><img src="https://d2ss6ovg47m0r5.cloudfront.net/badges/tindie-mediums.png" alt="I sell on Tindie" width="150" height="78"></a>

## Features

- ESP8266 MCU based Geiger Counter
- Low 5V power requirements
- Compatible with numerous Geiger-Muller tubes
- High Voltage circuit adjustment and feedback
- OLED display
- Integrated light and audio feedback

## Technical Specification

- Supply Voltage: 5V DC
- Supply Current: 130mA with WiFi at background
- 2.4GHz WiFi

## Compatible Geiger Muller Tubes

The ESPGeigerHW Geiger counter supports multiple different Geiger Muller Tubes. The hardware features adjustable mounting positions for tubes with the following lengths:

- Position 1: 85mm - 95mm ± 2.0
- Position 2: 100mm - 110mm ± 2.0
- Position 3: 110 - 115mm ± 2.0

The board also has additional jumpers for connecting tubes that aren't directly mountable.

The high-voltage circuit is tested reliable for tubes requiring up to 400V operation.

The following commonly available tubes are known to be compatible with the ESPGeigerHW Geiger counter

| Tube Name | Length | Operating Voltage | Ratio | Notes |
|---|---|---|---|---|
SBM-20 (СБМ-20) | 108 ± 3.5 | 400V | - | 
SBM-19 (СБМ-19) | 195mm | 400V | - | 
STS-15 (CTC-15) | 111mm | 390-400V | - | 
J305 | 105mm | 400V | - | 
J305 | 90mm | 400V | - | 
M4011 | 90mm | 400V | - | 
ROBOTRON 70 013 | 163mm | 500V | 600 / 0.001667 | 

### Possibly Compatible
{: .no_toc }

The following tubes should also be compatible but are currently untested

- LND-712 <https://www.lndinc.com/products/geiger-mueller-tubes/712/>/<https://www.pocketmagic.net/tube-lnd-712-end-window-alpha-beta-gamma-detector/>
- [LND-7312](https://www.lndinc.com/products/geiger-mueller-tubes/7312/)
- [SBT-9](https://www.pocketmagic.net/tube-sbt-9-end-window-geiger-tube/)
- [SBT-10A](https://www.pocketmagic.net/tube-sbt-10a-c%d0%b1t-10a/)
- [SBT-11A](https://www.gstube.com/data/3006/)
- [SI-180G](https://sites.google.com/site/diygeigercounter/technical/gm-tubes-supported)
- Philips 18504
- Beta-1
- Beta-2
- Gamma-7C