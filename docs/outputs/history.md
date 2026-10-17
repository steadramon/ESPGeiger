---
layout: page
title: History page
permalink: /output/history
parent: Outputs
nav_order: 2
---

# History page

The `/hist` page gives a longer term view of activity on the device. It shows totals for today and yesterday, a 24 hour bar chart, an hour by hour table and (when enabled) a lifetime totals card and an inter pulse interval histogram.

## Lifetime card

When lifetime tracking is enabled, a Lifetime card appears showing cumulative state since the device was first set up:

- **Clicks** is the total number of clicks ever recorded. Survives reboots.
- **Dose** is the cumulative absorbed dose using the configured tube ratio. It scales automatically: micro sieverts for small values, milli sieverts from 1000 µSv, full sieverts from 1 Sv.
- **Tracked** is the number of seconds the device has been counting toward the dose denominator. It pauses whilst the device is off and resumes from saved state on the next boot.
- **Install age** is wall clock time since first boot.
- **Avg CPM** is lifetime clicks divided by tracked seconds, multiplied by 60.

A **Reset lifetime** button at the bottom of the card zeros every field in the card, including the saved totals that normally survive a reboot.

## Today, yesterday and the day rate

The summary card at the top of the page shows five figures:

- **Today** is the total clicks since the most recent UTC midnight.
- **Yesterday** is the total clicks for the previous UTC day.
- **1h avg CPM** is the device's most recent rolling rate.
- **24h peak CPM** is the highest hourly rate seen in the last 24 hours.
- **Day rate vs yesterday** projects today's clicks to a full day at the current rate and compares that with yesterday.

The day rate shows a dash until the device has run through a full UTC day, at least one hour of today has passed and yesterday has at least one click.

## 24 hour bar chart and table

The bar chart shows the click count for each of the last 24 hours, with the current partial hour as the right hand bar.

The table below repeats the same data with timestamps in your browser's local time, the average CPM for that hour and (whilst the configured tube ratio is non zero) the derived µSv per hour.

## Inter pulse interval histogram

This is a histogram of the time gaps between consecutive clicks. Buckets double in size from `<64µs` up to `>=512s`, so one chart covers both slow background and a hot source. The data is cumulative since the last reboot.

### What healthy looks like

Background radiation is a Poisson process, which means consecutive inter pulse gaps follow an exponential distribution with a mean of `60 / CPM` seconds. The histogram should be right skewed, with most of the counts clustered around that mean bucket and a long tail to the right.

- At a typical 30 CPM background you would expect most pulses in the 1 to 4 second buckets.
- At 0.5 CPM (a well shielded counter, or very low background) you would expect most around 60 to 240 seconds.
- At 1000 CPM (a tube exposed to a hot source) you would expect most around 32 to 128 ms.

### What anomalies hint at

- A big spike in the leftmost bin (`<64µs`) usually means tube ringing, or the firmware counting a single physical pulse twice. Try raising the debounce, or check the discriminator threshold on the front end.
- Two distinct peaks suggest two pulse sources are mixing, for example real clicks plus electrical noise, or two physically separate tubes wired into the same input.
- An empty histogram means the tube isn't producing pulses, or the input pin isn't wired to the line the firmware is reading.

The histogram resets on reboot, so an unusual cluster from a one off event will fade after the next power cycle.
