---
layout: page
title: HTTP Endpoint Schemas
permalink: /dev/http-endpoints
nav_order: 25
parent: Testing & Development
---

# HTTP Endpoint Schemas

Response formats for the diagnostic endpoints listed on the [Web Portal](/output/webportal#http-endpoints) page.

## `/outputs`

A JSON object keyed by module name, describing each enabled third-party output. Disabled or unconfigured modules are left out; `{}` means none are active.

Keys: `mqtt`, `radmon`, `thingspeak`, `gmc`, `webhook`, `webapi` and `udpblip` when Local Broadcast is on.

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

Static identity of the device and firmware, plus the compiled-in modules. Runtime values (uptime, memory, RSSI) are in `/json`.

```json
{
  "ver": "0.8.0",
  "git": "06a2d9d",
  "env": "espgeigerlog",
  "date": "Apr 15 2026 12:34:56",
  "chip": "ESP8266",
  "flash": "ef4016 DIO 40MHz",
  "mac": "AABBCCDDEEFF",
  "host": "ESPGeiger-a1b2c3",
  "g_mod": "genpulse",
  "g_type": "pulse",
  "g_test": false,
  "g_pcnt": true,
  "modules": ["disp","neopx","button","ntp","mqtt","radmon","whook","sercmd","serout"]
}
```

| Field | Meaning |
|---|---|
| `ver` | Firmware version (release) |
| `git` | Short git commit the firmware was built from |
| `env` | PlatformIO build environment name (e.g. `espgeigerlog`, `esp32_pulse`) |
| `date` | Build timestamp (compile-time `__DATE__ __TIME__`) |
| `chip` | Chip model reported by the framework (`ESP8266` / `ESP32` / `ESP32-S2` etc.) |
| `flash` | Flash chip as booted: JEDEC id (ESP8266 only), mode and clock from the image header |
| `mac` | Device MAC address |
| `host` | Configured hostname |
| `g_mod` | Geiger model name. On serial builds this identifies the protocol (`GC10`, `GC10Next`, `MightyOhm`, `ESPG`). |
| `g_type` | Counter input type: `"pulse"`, `"serial"`, or `"none"` (UDP receiver and internal test builds) |
| `g_test` | `true` on test / simulation builds |
| `g_pcnt` | `true` if pulses are counted by the ESP32 PCNT hardware counter |
| `modules` | Module identifiers compiled into this firmware |

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
