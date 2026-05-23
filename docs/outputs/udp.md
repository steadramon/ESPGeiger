---
layout: page
title: UDP / OSC
permalink: /output/udp
parent: Outputs
nav_order: 10
---

# Local Broadcast (UDP/OSC)

ESPGeiger can broadcast click events and periodic stats to a UDP multicast group using the [Open Sound Control (OSC) 1.0](https://opensoundcontrol.stanford.edu/spec-1_0.html) wire format. Any OSC-aware tool on the same LAN (Pure Data, TouchDesigner, python-osc, Max/MSP, Node-RED) can subscribe and react to live radiation events without polling MQTT or HTTP.

A matching receiver firmware variant (`UDP-Receiver`) lets a tubeless ESP device mirror another ESPGeiger over the air. Display, MQTT, WebAPI, OLED and blip-LED all work as if a real tube were attached.

- Multicast group: `239.255.86.86` (configurable)
- UDP port: `57340` (configurable)
- Service announced via mDNS as `_osc._udp`

## Quick Start

You need two devices on the same WiFi: one ESPGeiger with a tube (the **producer**) and one with a `*_udp` build (the **receiver**).

**On the producer:**
1. Note its 6-hex chipid, visible on the home page or `/info`.
2. Go to **Config → Local Broadcast**, set Mode to `2` (telemetry + blips), Save.

**On the receiver:**
1. Flash an `esp8266_udp` / `esp8266oled_udp` / `esp32_udp` / `esp32oled_udp` build via the [Web Installer](https://install.espgeiger.com).
2. Join your WiFi via the captive portal, same network as the producer.
3. Open the device's web UI and go to **Config → Input**.
4. (Optional) Set **udprx_chipid** to the producer's 6-hex ID. Leave blank to auto-latch onto the first producer heard.
5. Save.

Within a few seconds the receiver's OLED page 2 (or `/status`) should show `RX <chipid>` and a loss percentage, and the CPM number should track the producer's.

### Not working?

- **Same WiFi network and subnet.** Multicast does not cross routers or guest networks; both devices need to be on the same AP / SSID.
- **Producer in Mode 2.** Mode 1 only sends stats every 30 s and produces no clicks.
- **Receiver actually flashed as `*_udp`.** Check `/info` on the receiver shows a UDP build target.
- **Loss percentage stuck high.** Try `udprx_rxmode = 1` (modem sleep, default) before `2` (none). `2` is usually worse on noisy LANs.
- **Wrong producer locked.** In auto-mode the receiver latches onto the first producer heard. Set the chipid explicitly if you have multiple.

## Output Modes (Producer)

Configured on the **Config → Local Broadcast** page.

| Mode | Behaviour |
|---|---|
| `0` (off) | No packets sent. Default. |
| `1` (telemetry only) | `/rad`, `/hv`, `/sys` periodic bursts. No per-click traffic. |
| `2` (telemetry + blips) | Periodic telemetry plus one `/click` per radiation event. |

## Source Modes (Receiver)

Configured on the **Config → Input** page (`udprx_mode`).

| Mode | Behaviour |
|---|---|
| `0` (specific) | Accept clicks from a specific producer. With `udprx_chipid` blank, auto-latch onto the first producer heard and lock to it. |
| `1` (sum) | Aggregate clicks from every producer heard. Up to 8 tracked. |

## Build Targets

| Target | Platform | Display |
|---|---|---|
| `esp8266_udp` | ESP8266 | headless |
| `esp8266oled_udp` | ESP8266 | OLED |
| `esp32_udp` | ESP32 | headless |
| `esp32oled_udp` | ESP32 | OLED |

## OSC Message Schema

### `/espg/<chipid>/click`

Emitted on every accepted click in mode 2.

```
,ii
  counter : int32   monotonic click number on the producer
  ts_ms   : int32   producer millis() at emit
```

### `/espg/<chipid>/rad`

Radiation telemetry. Periodic heartbeat (~30 s).

```
,ffsi
  cpm          : float    1-minute average
  usv          : float    µSv/h
  state        : string   "warming" | "healthy" | "warning" | "alert"
  total_clicks : int32    producer's lifetime click count
```

### `/espg/<chipid>/hv`

HV telemetry (HV-equipped builds only, same cadence as `/rad`).

```
,ffii
  reading_v  : float    measured HV (smoothed)
  target_v   : float    HV setpoint
  duty       : int32    PWM duty (0-1023)
  trim       : int32    autotrim adjustment in duty units (signed)
```

### `/espg/<chipid>/sys`

System telemetry. Every other `/rad` (~60 s).

```
,iiii
  uptime_s      : int32   producer seconds since boot
  rssi          : int32   WiFi signal strength
  lps           : int32   main loops per second
  tick_max_us   : int32   worst main-loop iteration in last window
```

### `/espg/<chipid>/env`

Environment telemetry. Only emitted when a supported [environment sensor](/configuration/env) is wired in. Same cadence as `/sys` (~60 s).

```
,fff
  temp_c      : float    Temperature in °C, NaN when absent
  humidity    : float    Relative humidity %, NaN on BMP280-only builds
  pressure    : float    Atmospheric pressure in hPa, NaN on AHT-only builds
```

Fields that the local sensor doesn't provide are encoded as IEEE 754 NaN, not omitted - the tag string is always `,fff`. Receivers should check `isnan()` on each value.

## Receive Examples

### python-osc

```python
from pythonosc import dispatcher, osc_server

def on_click(addr, counter, ts_ms):
    print(f"{addr} counter={counter} ts_ms={ts_ms}")

def on_rad(addr, cpm, usv, state, total_clicks):
    print(f"{addr} {cpm:.1f} CPM ({usv:.3f} uSv/h) {state}")

d = dispatcher.Dispatcher()
d.map("/espg/*/click", on_click)
d.map("/espg/*/rad",   on_rad)

server = osc_server.ThreadingOSCUDPServer(
    ("239.255.86.86", 57340), d, multicast=True)
server.serve_forever()
```

### Pure Data / TouchDesigner / Max

Subscribe to multicast group `239.255.86.86` on port `57340`. Filter on path `/espg/*/click` for live events, `/espg/*/rad` for radiation telemetry, `/espg/*/hv` for HV readings, or `/espg/*/sys` for system health.

## Discovery (mDNS)

When mode > 0 the producer announces an `_osc._udp` service with the configured port. TXT records: `id` (6-hex chipid), `fname` (friendly name), `group` (multicast IP). Discovery is optional; pre-configured receivers just need the group and port.

## Reliability and tuning

- **Loss handling**: every `/click` carries a running counter so the receiver detects loss as a sequence gap and gap-fills automatically. Cumulative loss % is on the OLED and `/status`.
- **Send throttle**: high-CPS producers cap `/click` emit rate at 20 pps. Counter accuracy is preserved because each packet carries the producer's running total.
- **RX sleep mode** (`udprx_rxmode`): `1` (modem sleep, default) is the right balance on most networks. `0` saves more power, `2` keeps the radio always on but is often worse on busy LANs.

## Build Flags

All compile-time tunables (group, port, intervals, fleet jitter, throttle thresholds) are documented in [PlatformIO Build Options → UDP / OSC Output](/install/platformio#udp--osc-output).
