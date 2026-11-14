---
layout: page
title: HTTP Endpoint Schemas
permalink: /dev/http-endpoints
nav_order: 25
parent: Testing & Development
---

# HTTP Endpoint Schemas

Response formats for the diagnostic endpoints listed on the [Web Portal](/output/webportal#http-endpoints) page.

If a web password is set, every endpoint here needs HTTP Basic auth, user `admin`.

## `/outputs`

A JSON object keyed by output name, describing each enabled third-party output. Disabled or unconfigured modules are left out; `{}` means none are active. A module quarantined by the boot guard shows under its module short name as `{"quarantined":true}`. Each output's entry is sent on its own: one that would pass 255 bytes is left out, the rest still come.

Keys: `mqtt`, `radmon`, `thingspeak`, `gmc`, `webhook`, `webapi` and `udpblip` when Local Broadcast is on. `/about` lists module short names, which differ for three of them: `whook`, `thgspk`, `wapi`.

```json
{
  "mqtt":       {"ok":true,  "age":42},
  "radmon":     {"ok":true,  "age":17},
  "thingspeak": {"ok":false, "age":91},
  "gmc":        {"ok":true,  "age":230},
  "webhook":    {"ok":false, "age":null}
}
```

| Field | Meaning |
|---|---|
| `ok` | `true` if the last submission was accepted. For MQTT this means connected plus a successful recent publish. |
| `age` | Seconds since the last submission attempt completed, or `null` if none has been attempted yet. |

`udpblip` has a different shape: `{"mode":N,"group":"239.x.x.x","port":N,"fails":N}`.

Use it as a lightweight health check for dashboards, scripts or Home Assistant REST sensors.

## `/about`

Identity of the device and firmware, the chip and WiFi details the `/info` page leaves out, and the compiled-in modules. Live values (uptime, memory, RSSI) are in `/json`.

In safe mode only the core routes exist: `/json`, `/status`, `/hist` and `/webcmd` answer 404; `/about`, `/outputs` and `/metrics` still work.

```json
{
  "ver": "0.8.0",
  "git": "06a2d9d",
  "env": "espgeigerlog",
  "date": "Apr 15 2026 12:34:56",
  "chip": "ESPG-LOG",
  "flash": "ef4016 DIO 40MHz",
  "mac": "AABBCCDDEEFF",
  "host": "ESPGeiger-a1b2c3",
  "core": "3.1.2",
  "boot": 31,
  "fsize": 4194304,
  "freal": 4194304,
  "mhz": 80,
  "gw": "192.168.1.1",
  "sn": "255.255.255.0",
  "bssid": "E4:95:6E:45:15:B4",
  "tx": 20.50,
  "phy": "11n",
  "sleep": "Modem",
  "auto": true,
  "g_mod": "genpulse",
  "g_type": "pulse",
  "g_test": false,
  "g_pcnt": true,
  "fl": 127,
  "modules": ["sys","net","wifi","scmd","ota","counter","life","led","disp","neopx","btn","sdcard","ntp","mqtt","radmon","whook","sout"]
}
```

| Field | Meaning |
|---|---|
| `ver` | Firmware version (release) |
| `git` | Short git commit the firmware was built from |
| `env` | PlatformIO build environment name (e.g. `espgeigerlog`, `esp32_pulse`) |
| `date` | Build timestamp (compile-time `__DATE__ __TIME__`) |
| `chip` | Board name on ESPGeiger kits (`ESPG-HW`, `ESPG-LOG`, `ESPG-LT`), else the chip model reported by the framework (`ESP8266` / `ESP32` / `ESP32-S2` etc.) |
| `flash` | Flash chip as booted: JEDEC id (ESP8266 only), mode and clock from the image header |
| `mac` | Device MAC address |
| `host` | Configured hostname |
| `core`, `boot` | ESP8266 only: Arduino core version and bootloader version |
| `model`, `rev`, `sdk`, `temp` | ESP32 only: chip model, chip revision, SDK version, die temperature in C (`null` if the chip can't read it) |
| `fsize` | Flash size the image was built for, in bytes |
| `freal` | ESP8266 only: flash size the chip reports. `/info` warns when it differs from `fsize` |
| `mhz` | CPU frequency |
| `gw`, `sn`, `bssid` | Gateway, subnet mask and the BSSID of the access point it's joined to |
| `tx` | WiFi transmit power in dBm, `null` if the core doesn't report it |
| `phy`, `sleep`, `auto` | WiFi PHY mode, radio sleep mode, and whether WiFi auto-reconnect is on |
| `g_mod` | Geiger model name. On serial builds this identifies the protocol (`GC10`, `GC10Next`, `MightyOhm`, `ESPG`). |
| `g_type` | Counter input type: `"pulse"`, `"serial"`, or `"none"` (UDP receiver and internal test builds) |
| `g_test` | `true` on test / simulation builds |
| `g_pcnt` | `true` if pulses are counted by the ESP32 PCNT hardware counter |
| `fl` | Feature flags, the same bits WebAPI sends as `fl` |
| `modules` | Every module compiled into this firmware, core ones included. Each is the module's short name; three differ from the `/outputs` keys (`whook`, `thgspk`, `wapi`) |

## `/json?raw=1`

By default `mem` is lightly smoothed and the device-load fields are left out to keep the frequently polled `/json` small. `?raw=1` reports `mem` un-smoothed and adds:

| Field | Description |
|---|---|
| `tick` | Smoothed microseconds spent in the once-per-second housekeeping pass. Lower is better. |
| `t_max` | Worst-case `tick` in the last 60 seconds. |
| `lps` | Main-loop iterations per second, un-smoothed (last full second). |
| `frag` | Heap fragmentation, percent |
| `lfb` | Largest contiguous free block in bytes |
| `lfblow` | Largest-free-block low-water mark since boot, in bytes |

`lfb` / `lfblow` are the honest measure of memory health: free heap can look fine while fragmentation shrinks the largest block that can be allocated.
