---
layout: page
title: OLED
permalink: /output/oled
parent: Outputs
nav_order: 10
---

# OLED Display Output

![Img](../img/OLEDExample.png)

# Display behaviour

Three settings in the **Display** section of the [Config page](/configuration#display-settings-oled-builds) control when the OLED is on:

- **Idle time (s)** *(push-button builds only)* - seconds of inactivity before the display turns off after a tap. `0` falls back to the On/Off time schedule below.
- **On time / Off time** - daily schedule; outside the window the display is off. Leave both blank for "always on". Window crosses midnight if `from > to` (e.g. `22:00`-`07:00`).
- **Brightness** - 0-100 %.

On page 1 a warning sign left of the CPM shows the warning level is reached; at the alert level the sign sits in a white box.

On builds **without** a push button, only the schedule applies.

On builds **with** a push button, the resolution order is:

1. Long-press override active → display always on.
2. Otherwise, if Idle time > 0 → on for Idle time seconds after each tap.
3. Otherwise (Idle time = 0) → follow On/Off time schedule, with a 30-second wake window after any tap so you can check readings during off-hours.

# Push-button operations

## Single press

Cycles through the information pages. If the display is off, the first tap turns it on and shows page 1.

On builds with an SD card or HV, a fault puts one line on the screen: *SD card error* (the card failed or is full, not a card missing at boot) or *HV fault*. It stays until a press, and that press only clears it. The fault wakes the display, but not in the off hours of the On/Off schedule: it shows at the next wake.

## Long press

Toggles the always-on override. Long-press once to keep the display on; long-press again to return to the Timeout and schedule.

The screen shows **Screen always on** or **Screen auto off** for a moment. The status LED gives one long pulse for always on, two short blinks for auto off.

# Pin configuration

Set **SDA pin** and **SCL pin** in the **I2C Bus** section of the Config page (System tab), then restart. Not shown on kits with fixed pins.

# Browser screen viewer

Open **`/screen`** to see a live copy of the display in your browser, with a button for virtual taps. It works with no display attached, so a headless OLED build still shows what the panel would.

| Path | Returns | Use |
|---|---|---|
| `/screen` | HTML viewer page | Interactive view in the browser |
| `/screen.bin` | 4 byte header (width, height, 16-bit little-endian) then the framebuffer | Scripting (column-packed pages, 1 bit per pixel) |
| `/screen.bmp` | Monochrome BMP image | One-shot screenshot |
| `/screen/tap` | `ok` | Virtual short press of the user button (push-button builds), snoozes an alarm as the button does |
