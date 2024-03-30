---
layout: page
title: GMC
permalink: /output/gmc
parent: Outputs
nav_order: 10
---

# GMC World Map

ESPGeiger allows you to contribute to the global monitoring of radiation levels by submitting your Geiger counter readings to [GMCMap](http://www.gmcmap.com/). GMCMap is provided by [GQ Electronics](https://www.gqelectronicsllc.com), who also design, build and sell a range of Geiger Counters.

## Setup

1. __Register on www.gmcmap.com__: Create a free account on the www.gmcmap.com website.
2. __Add a Geiger Counter__: Login to your www.gmcmap.com account and navigate to [Manage Device](https://www.gmcmap.com/manageDevice.asp) and click [Add a device](https://www.gmcmap.com/addGeigerCounter.asp). 
3. __Record the configuration options__: From [Manage Device](https://www.gmcmap.com/manageDevice.asp), record the assigned __Geiger Counter ID__ of your new device. From [My Profile](https://www.gmcmap.com/myProfile.asp) record your __Account ID__.
4. __Configure ESPGeiger__: In the ESPGeiger web interface, click __Config__ and enter your GMC __Account ID__ and __Geiger Counter ID__ in the relevant fields. ESPGeiger will submit your CPM, CPM5 and μSv readings to GMC every 300 seconds.

ESPGeiger will report the GMC submission status in the Web Interface console. Once your ESPGeiger is set up and transmitting data, it will appear as a station on the GMC World Map.