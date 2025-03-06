---
layout: page
title: Offline Mode
permalink: /configuration/offlinemode
nav_order: 5
parent: Configuration
---

# Offline Mode

ESPGeiger offers an offline mode for situations where network connectivity is unavailable or unnecessary. This mode allows the device to function as a standalone device. This is especially useful on the [ESPGeiger-HW](/hardware/espgeigerhw), [ESPGeiger-Log](/hardware/espgeigerlog) and [Cajoe GC-ESP32](/hardware/cajoeiot) hardware devices - focusing solely on local radiation detection and data logging.

## Entering Offline Mode

To manually activate offline mode:

1.  **Ensure the push button module is installed and enabled.** This feature is only available on builds with the push button.
2.  **Power off or reset the ESPGeiger module.**
3.  **Press and hold the push button.**
4.  **While holding the button, power on or reset the ESPGeiger module.**
5.  **Continue holding the button until the device begins its startup sequence.**

The ESPGeiger will now boot into offline mode.

## Functionality in Offline Mode

When operating in offline mode:

* **All online features are disabled.** This includes:
    * Wi-Fi connectivity.
    * Data transmission to online platforms.
    * Time synchronization via network time protocols.
* **The device focuses on local radiation detection.** The Geiger counter will operate normally, and data will be displayed normally.
* **The device will not attempt to connect to any Wi-Fi networks.** This is a key distinction from the standard startup behavior.

## Important Considerations

* **Returning to Online Mode:** To restore online functionality, you must reboot the ESPGeiger module without holding the push button.
* **Wi-Fi Connection Attempts:** During a standard startup (without entering offline mode), if the configured Wi-Fi access point is unavailable, ESPGeiger will continuously attempt to connect. This is different from offline mode, which completely disables Wi-Fi.
* **Purpose:** Offline mode is ideal for scenarios where:
    * Network connectivity is unreliable or non-existent.
    * You wish to minimize power consumption by disabling Wi-Fi.
    * You only require local radiation data logging.

By utilizing offline mode, you can ensure that your ESPGeiger remains functional and continues to collect data even in environments without network access.
