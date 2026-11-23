---
layout: page
title: Pulse Counters
permalink: /hardware/pulse
parent: Hardware
nav_order: 2
---

# Pulse Counters

ESPGeiger supports pulse-based Geiger counters that send an I/O pulse for each detected count.

| Counter | Image | Notes |
|---|---|---|
[BR-6](https://s.click.aliexpress.com/e/_c3fCxZZr) | ![BR-6](img/br-6.jpg#img-thumbnail) | 
[CAJOE RadiationD-v1.1](https://s.click.aliexpress.com/e/_c4W8VJyH) (and other clones) | ![CAJOE RadiationD-v1.1](img/cajoev11.jpg#img-thumbnail) | Compatible with CAJOE RadiationD v0.8/1.1/1.2 - v1.1 is by far the most common. Pulse out marked as `VIN` on some models
[CAJOE GC-ESP32](https://s.click.aliexpress.com/e/_c3KwtTrL) | ![CAJOE GC-ESP32](img/cajoeiot.png#img-thumbnail) | ESPGeiger is also [compatible with the ESP32 on board](/hardware/cajoeiot), no need for an external ESP.
[DiY-GDC](https://www.ebay.com/usr/impexeris) | ![MightyOhm](img/DiY-GDC.jpg#img-thumbnail) | 
[DIY Geiger Counter Kit - Geiger M4011](https://s.click.aliexpress.com/e/_c3lOeBsH) | ![Geiger M4011](img/geigerm4011.png#img-thumbnail) | 
[DIY GeigerKit](https://sites.google.com/site/diygeigercounter) | ![DIY GeigerKit](img/diygeiger.jpg#img-thumbnail) | 
[GeigerHV](https://www.ebay.co.uk/usr/geigerhv) | | 
[GGreg20](https://www.tindie.com/stores/iotdev/) | ![GGreg20](img/ggreg.jpg#img-thumbnail) | 
[MightyOhm Kit](https://www.tindie.com/stores/mightyohm/) | ![MightyOhm](img/mightyohm.jpg#img-thumbnail) | 
[NetIO GC10](https://www.ebay.co.uk/usr/pelorymate) | ![NetIO GC10](img/gc10.jpg#img-thumbnail) | 
[RHElectronics Counters](https://www.rhelectronics.store/diy-geiger-counter-kit) | ![RH-K-GK-2-A](img/RH-K-GK-2-A.jpg#img-thumbnail) | 
[SparkFun Geiger Counter](https://www.sparkfun.com/products/retired/11345) | ![11345](img/sparkfun11345.jpg#img-thumbnail) |
[Yaorea YRG01](https://s.click.aliexpress.com/e/_c3BZh7KV) | ![Yaorea YRG01](img/yrg01.png#img-thumbnail) | Cheap handheld counter with a [pulse-tap mod](https://www.blinkyslab.co.uk/2025/04/a-little-gem-the-yaorea-yrg01-pt2-modding-for-radmonorg) to feed clicks into an ESPGeiger pulse input.

Special thanks to all the above original creators. Without your hard work and inspiration this project would not be possible!

## Tube setting

**Tube** (Config, Input) names the GM tube. Picking one and saving sets the ratio, the dead time and α/β/γ from this table, where they are still the build default or the previous tube's figure. Values you set yourself are kept. A tube with no dead time listed puts the default back. **Other** changes nothing.

| Tube | Ratio (CPM per µSv/h) | Dead time (µs) | Detects | Ratio from |
|---|---|---|---|---|
| SBM-20 | 158 | 190 | β γ | [uRADMonitor][ur] |
| SBM-19 | 667 | 250 | β γ | [uRADMonitor][ur] |
| SBM-21 | 20.8 | | β γ | [uRADMonitor][ur] |
| STS-5 | 150 | | β γ | [uRADMonitor][ur] |
| SI-22G | 583 | | β γ | [uRADMonitor][ur] |
| J305 | 175 | | β γ | [J305 note][j], Cs-137 |
| J321 | 151 | | β γ | as M4011 |
| M4011 | 151 | | β γ | [Instructables][i] |
| LND 712 | 168 | 90 | α β γ | [uRADMonitor][ur] |
| VacuTec 70 013 | 600 | 100 | γ | datasheet, per µGy/h, Cs-137 |

Dead times are from the datasheets. Ratios vary with the source a tube was measured against, see the [tube table](/hardware/espgeigerhw#compatible-geiger-muller-tubes). LND 712 and VacuTec 70 013 run at about 500 V, above the ESPGeigerHW's 400 V.

[ur]: https://github.com/radhoo/uradmonitor_kit1/blob/master/code/124/geiger/detectors.cpp
[j]: https://iot-devices.com.ua/en/geiger-tube-j305-how-to-calculate-the-conversion-factor-of-cpm-technical-note-en/
[i]: https://instructables.com/Geiger-Counter

### uRADMonitor tube ids

uRADMonitor's upload names the tube by number (field `10`, [detectors.h](https://github.com/radhoo/uradmonitor_kit1/blob/master/code/124/geiger/detectors.h)). For a future uRADMonitor upload:

| Tube | uRADMonitor id |
|---|---|
| Other, J321, VacuTec 70 013 | 0 |
| SBM-20 | 1 |
| SBM-19 | 3 |
| LND 712 | 4 |
| SI-22G | 6 |
| STS-5 | 7 |
| SBM-21 | 9 |
| J305 | 14 |
| M4011 | 15 |

