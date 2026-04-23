---
layout: page
title: OTA (Firmware Update)
permalink: /install/ota
nav_order: 30
parent: Install
---

# OTA Firmware Update

Once ESPGeiger is installed on a device, future updates are done over Wi-Fi
from your browser. No USB cable, no toolchain, no command line.

If you've never flashed the device before, use the [Web Installer](/install/webinstall)
instead — OTA only works on a device that's already connected to your Wi-Fi
and reachable on your network.

## 1. Download the firmware

Grab the latest `.bin` file for your board from the
[GitHub Releases page](https://github.com/steadramon/ESPGeiger/releases).

Every release includes one `.bin` per build variant. The filename tells you
which one you need — **it must match the variant you originally installed**.
For example:

- `esp8266_pulse.v1.2.3.bin` — ESP8266 with a pulse-output tube
- `esp8266_serial.v1.2.3.bin` — ESP8266 with a serial-output counter (GC10, MightyOhm, …)
- `espgeigerhw.v1.2.3.bin` — ESPGeiger-HW board
- `espgeigerlog_pulse.v1.2.3.bin` — ESPGeiger Log with a pulse tube
- …etc

Not sure which one you're running? The web portal shows the build name on
the **Info** page as `BUILD_ENV` — match that.

> ⚠️ Flashing the wrong variant will either refuse to boot or boot with the
> wrong hardware assumptions (wrong pins, wrong counter type). If this
> happens, recover with the [Web Installer](/install/webinstall) over USB.

## 2. Open the device's web portal

In a browser, go to your device's address. Either:

- `http://<ip-address>` — e.g. `http://192.168.1.167` (find it in your router's DHCP list), **or**
- `http://<hostname>.local` — e.g. `http://ESPGeiger-abcdef.local`

## 3. Click **Update** on the main menu

From the main portal page, click the **Update** button. This takes you to
the firmware upload page.

## 4. Choose the `.bin` file

Click **Browse…** and pick the `.bin` you downloaded in step 1.

## 5. Click **Update**

Click the **Update** button on the upload page. The device:

- Shows *"Update in progress…"* on its OLED (if fitted)
- Lights the activity LED
- Stops all regular activity (MQTT publishes, WebAPI posts, serial output)
  to avoid interfering with the flash write
- Receives the new firmware, writes it, verifies it, and reboots

Total time is typically **15–30 seconds** depending on your Wi-Fi. Don't
close the browser tab until the upload bar finishes.

## After the update

The device reboots into the new firmware. Wait about 10 seconds, the
page should refresh. The version on the **Info** page should show the
new release tag.

Your settings (Wi-Fi credentials, MQTT config, tube ratio, etc.) survive
the update — only the firmware code is replaced.

## If it fails

- **"Update failed"** with a tiny bar — the device recovers automatically,
  you'll still be able to reach the portal on the old firmware. Try again.
- **Device unreachable after update** — usually the wrong `.bin` variant.
  See the warning in step 1. Recover with the Web Installer over USB.
- **Upload stalls partway** — usually Wi-Fi instability. Move closer to the
  router or use a wired-closer AP, and retry.

## Advanced: command-line OTA

If you prefer the CLI, `espota.py` (ships with the Arduino ESP core) does
the same thing in one command:

```
python espota.py -i 192.168.1.167 -p 8266 -f esp8266_pulse.v1.2.3.bin
```
