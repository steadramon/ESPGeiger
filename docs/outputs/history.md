---
layout: page
title: History page
permalink: /output/history
parent: Outputs
nav_order: 2
---

# History page

The `/hist` page gives a longer term view of activity on the device. It shows totals for today and yesterday, a 24 hour bar chart, an hour by hour table, and (when enabled) a lifetime totals card and an inter pulse interval histogram.

## Lifetime card

When lifetime tracking is enabled, a Lifetime card appears showing cumulative state since the device was first set up:

- **Clicks** is the total number of clicks ever recorded. Survives reboots.
- **Dose** is the cumulative absorbed dose using the configured tube ratio. It scales automatically: micro sieverts for small values, milli sieverts at 1000 µSv and above, and full sieverts at 1 Sv and above.
- **Tracked** is the number of seconds the device has been counting toward the dose denominator. It pauses whilst the device is off and resumes from saved state on the next boot.
- **Install age** is wall clock time since first boot.
- **Avg CPM** is lifetime clicks divided by tracked seconds, multiplied by 60.

A **Reset lifetime** button at the bottom of the card zeros every field in the card, including the saved totals that normally survive a reboot.

## Inter pulse interval histogram

This is a histogram of the time gaps between consecutive clicks. Buckets are log base 2 and double in size from `<64µs` up to `>=512s`. The data is cumulative since the last reboot.

## Today, yesterday and the day rate

The summary card at the top of the page shows five figures:

- **Today** is the total clicks since the most recent UTC midnight.
- **Yesterday** is the total clicks for the previous UTC day.
- **1h avg CPM** is the device's most recent rolling rate.
- **24h peak CPM** is the highest hourly rate seen in the last 24 hours.
- **Day rate vs yesterday** is a rate based percentage comparison.

The "Day rate vs yesterday" line isn't a raw total comparison. Comparing eight hours of today against twenty four hours of yesterday would always read as a big drop, which isn't useful. Instead, today's clicks are projected to a full 24 hour day at the current rate and that projection is compared to yesterday's full day.

The line shows a dash until all of these conditions are met:

- The device has been running long enough that yesterday was a full UTC day. The boot time must be before yesterday's UTC midnight.
- At least one hour of today has passed, so the projection isn't wild.
- Yesterday has at least one recorded click.

A freshly flashed device stays empty on this line until it has observed its first full day.

## 24 hour bar chart and table

The bar chart shows the click count for each of the last 24 hours, with the current partial hour as the right hand bar. The bars use the same accent colour as the rest of the UI.

The table below repeats the same data with timestamps in your browser's local time, the average CPM for that hour, and (whilst the configured tube ratio is non zero) the derived µSv per hour.

### Why log buckets

A radiation counter handles a huge dynamic range. Background pulses might be tens of seconds apart whilst a hot source could fire several times in a millisecond. A linear histogram would need thousands of bins to cover both regimes. The log base 2 layout covers the same range in 25 buckets, which only costs the device 100 bytes of RAM.

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
