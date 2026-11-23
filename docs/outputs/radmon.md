---
layout: page
title: Radmon.org
permalink: /output/radmon
parent: Outputs
nav_order: 10
---

# Radmon.org Output

ESPGeiger allows you to contribute to the global monitoring of radiation levels by submitting your Geiger counter readings to [radmon.org](https://radmon.org/). Radmon is a free community for enthusiasts interested in tracking background radiation and potential events.

## Setup

1. __Register on radmon.org__: Create an account on the [radmon.org](https://radmon.org/index.php/register) website.
2. __Configure Data Sending Password__: Login to your radmon.org account and navigate to the [Control Panel](https://radmon.org/index.php/control-panel). Within the control panel, locate the section for setting up your station. Make a note of the `Data Sending Password` for your station.
3. __Configure ESPGeiger__: On the [Config page](/configuration), open the __Upload__ tab, tick __Enable__ under __Radmon__ and enter your radmon.org `username` and the `Data Sending Password` in the relevant fields. ESPGeiger submits your `CPM` to radmon.org every 60 seconds by default (**Interval**, 30 to 1800 s, under the advanced settings). Up to 90 s it sends the 1-minute CPM, up to 450 s the 5-minute average, above that the 15-minute average. The Data Sending Password goes to radmon.org in the request URL over plain HTTP (their API), so use one you don't use anywhere else.

**Send location** (off by default) also sets your station's place on the radmon.org map from the **Latitude** and **Longitude** on the System tab: once after each restart and after you change it, with the reading sent straight after. **Fuzz location** (advanced, on) sends it to about 1 km: 2 decimal places plus a third that is fixed for your device, so it doesn't jump around and stations in the same square don't sit on top of each other. Untick it to send 3 decimal places, about 100 m.

**Send conversion factor** (off by default) sets your station's factor on radmon.org to 1 / your ratio, so radmon.org's µSv/h matches the device. **Send warn/alert** (off by default) sets radmon.org's warning and alert levels to your Warning and Alert CPM and turns its alert emails on; with both at 0 (off) it turns them off, and with only one at 0 nothing is sent. Each goes once after a restart and again when the value changes, straight before a reading.

ESPGeiger will report the radmon.org submission status in the Web Interface console. Once your ESPGeiger is set up and transmitting data, it will appear as a station on the radmon.org map and be listed in the [Stations list](https://radmon.org/index.php/stations).

Plain http: if the site goes https-only the console says `Site moved (https?), can't follow` and every upload fails.