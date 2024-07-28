---
layout: page
title: ThingSpeak
permalink: /output/thingspeak
parent: Outputs
nav_order: 10
---

# ThingSpeak Output

![ThingSpeak](/img/thingspeak.png)

ESPGeiger can be configured to send to a ThingSpeak Channel.

## Setup

1. __Register on ThingSpeak__: Create an account on the [thingspeak.com](https://thingspeak.com/) website.
2. __Setup New Channel__: Login to your thingspeak.com account. Click "New Channel" from the Channels page.
3. __Configure New Channel__: Name and Describe the Channel. Set up the Field variables as below:
| Field | Description |
|---|---|
Field 1 | `CPM`
Field 2 | `μSv`
Field 3 | `CPM5`
Field 4 | `CPM15`
4. __Confirm New Channel__: Click __Save Channel__. Make a note of the Channel ID.
5. __Configure ESPGeiger__: In the ESPGeiger web interface, click __Config__ and enter your thingspeak.com `Channel ID` in the relevant fields. ESPGeiger will submit your readings to ThingSpeak every 90 seconds.
