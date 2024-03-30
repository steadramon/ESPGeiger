---
layout: page
title: Radmon.org
permalink: /output/radmon
parent: Outputs
nav_order: 10
---

# Radmon.org

ESPGeiger allows you to contribute to the global monitoring of radiation levels by submitting your Geiger counter readings to [radmon.org](https://radmon.org/). Radmon is a free community for enthusiasts interested in tracking background radiation and potential events.

## Setup

1. __Register on radmon.org__: Create an account on the [radmon.org](https://radmon.org/index.php/register) website.
2. __Configure Data Sending Password__: Login to your radmon.org account and navigate to the [Control Panel](https://radmon.org/index.php/control-panel). Within the control panel, locate the section for setting up your station. Make a note of the `Data Sending Password` for your station.
3. __Configure ESPGeiger__: In the ESPGeiger web interface, click __Config__ and enter your radmon.org `username` and the `Data Sending Password` in the relevant fields. ESPGeiger will submit your CPM readings to radmon.org every 60 seconds.

ESPGeiger will report the radmon.org submission status in the Web Interface console. Once your ESPGeiger is set up and transmitting data, it will appear as a station on the radmon.org map and be listed in the [Stations list](https://radmon.org/index.php/stations).