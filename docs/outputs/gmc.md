---
layout: page
title: GMC
permalink: /output/gmc
parent: Outputs
nav_order: 10
---

# GMC World Map Output

ESPGeiger allows you to contribute to the global monitoring of radiation levels by submitting your Geiger counter readings to [GMC World Map](http://www.gmcmap.com/). GMC World Map is provided by [GQ Electronics](https://www.gqelectronicsllc.com), who also design, build and sell a range of Geiger Counters.

## Setup

1. __Register on www.gmcmap.com__: Create an account on the [GMC World Map](https://www.gmcmap.com/userRegister-x.asp) website.
2. __Add a Geiger Counter__: Login to your www.gmcmap.com account and navigate to [Manage Device](https://www.gmcmap.com/manageDevice.asp) and click [Add a device](https://www.gmcmap.com/addGeigerCounter.asp). 
3. __Record the configuration options__: From [Manage Device](https://www.gmcmap.com/manageDevice.asp), record the assigned `Geiger Counter ID` of your new device. From [My Profile](https://www.gmcmap.com/myProfile.asp) record your `Account ID`.
4. __Configure ESPGeiger__: In the ESPGeiger web interface, click __Config__ and enter your GMC `Account ID` and `Geiger Counter ID` in the relevant fields. ESPGeiger will submit your `CPM`, `CPM5` and `μSv` readings to GMC every 300 seconds.

ESPGeiger will report the GMC submission status in the Web Interface console. Once your ESPGeiger is set up and transmitting data, it will appear as a station on the GMC World Map.