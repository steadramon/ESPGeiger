---
layout: page
title: UDP / OSC
permalink: /output/udp
parent: Outputs
nav_order: 10
---

# Local broadcast (UDP/OSC)

ESPGeiger can broadcast click events and periodic stats to a UDP multicast group using the [Open Sound Control (OSC) 1.0](https://opensoundcontrol.stanford.edu/spec-1_0.html) wire format. Any OSC-aware tool on the same LAN (Pure Data, TouchDesigner, python-osc, Max/MSP, Node-RED) can subscribe and react to live radiation events without polling MQTT or HTTP.

A matching receiver firmware variant (`UDP-Receiver`) lets a tubeless ESP device mirror another ESPGeiger over the air: display, MQTT, WebAPI, OLED and blip-LED all work as if a real tube were attached.

- Multicast group: `239.255.42.42` (site-local, configurable)
- UDP port: `57340` (configurable)
- Service announced via mDNS as `_osc._udp`

## Output Modes

Configured on the **Config → Local broadcast** page.

| Mode | Behaviour |
|---|---|
| `0` (off) | No packets sent. Default. |
| `1` (stats only) | Emits `/stats` every 30 s. No per-click traffic. |
| `2` (stats + blips) | `/stats` plus one `/click` per radiation event, OSC-bundled when bursts ≥ 2 clicks. |

## OSC Message Schema

### `/espg/<chipid>/click`

Emitted on every accepted click in mode 2.

```
,ii
  counter : int32   monotonic click number on the producer
  ts_ms   : int32   producer millis() at emit
```

Current rate is intentionally not carried per-click - subscribe to `/stats` for CPM, or derive rate from click cadence over a window.

### `/espg/<chipid>/stats`

Periodic heartbeat (30 s ± random per-boot jitter, see "Stats Jitter" below).

```
,fffsii
  cpm       : float    1-minute average
  usv       : float    µSv/h
  hv        : float    HV reading on HW builds, else 0
  state     : string   "warming" | "healthy" | "warning" | "alert"
  rssi      : int32    WiFi signal strength
  uptime_s  : int32    producer seconds since boot
```

### OSC Bundles

When ≥ 2 clicks fire in the same loop pass, `/click` messages are wrapped in an OSC `#bundle` (up to 10 per packet, `UDPBLIP_BUNDLE_MAX`) to cut WiFi airtime. Receivers unpack bundles transparently; every reasonable OSC library dispatches a callback per inner message.

For sustained hot sources above `UDPBLIP_MAX_BURST=200` clicks/loop (~4 000 cps, ~240 000 CPM ceiling), the producer collapses the burst into a single summary event rather than fragmenting across many packets.

## Stats Jitter

The first `/stats` emission is offset by a per-device `GRNG`-seeded random delay (default `UDPBLIP_STATS_JITTER_MS=7500` ms). A fleet of 100 devices booting together after a power-cut spreads its stats packets across the 7.5 s window, averaging 75 ms between emissions, well clear of the receiver's lwIP UDP mbox (6-deep on ESP8266).

The phase shift persists across all subsequent cycles. Override at compile time with `-DUDPBLIP_STATS_JITTER_MS=N`.

## Discovery (mDNS)

When mode > 0 and uptime ≥ 8 s, the producer announces an `_osc._udp` service with the configured port and the following TXT records:

| Key | Value |
|---|---|
| `id` | 6-hex chipid |
| `fname` | Friendly name from device prefs |
| `group` | Multicast group IP |

This follows the OSCQuery convention used by TouchDesigner / Ableton etc. Discovery is optional; pre-configured receivers just need the group + port.

# UDP Receiver

The `UDP-Receiver` firmware variant takes the multicast feed of one producer and behaves as if a real Geiger tube were attached. All outputs (MQTT, WebAPI, OLED, blip-LED) work unchanged; CPM, µSv/h and history all derive from the received clicks. Useful as:

- A wall-mounted **remote display** in another room from the tube
- A **mirror** for backup logging or alternative output sinks
- A **fleet aggregator** in sum mode, totalising clicks from many producers

## Quick Start

You need two devices on the same WiFi: one ESPGeiger with a tube (the **producer**) and one with a `*_udp` build (the **receiver**).

**On the producer:**
1. Note its 6-hex chipid, visible on the home page or `/info`.
2. Go to **Config → Local broadcast**, set Mode to `2` (stats + blips), Save.

**On the receiver:**
1. Flash an `esp8266_udp` / `esp8266oled_udp` / `esp32_udp` / `esp32oled_udp` build via the [Web Installer](https://install.espgeiger.com).
2. Join your WiFi via the captive portal, same network as the producer.
3. Open the device's web UI and go to **Config → Input**.
4. (Optional) Set **udprx_chipid** to the producer's 6-hex ID. Leave blank to auto-latch onto the first producer heard.
5. Save.

Within a few seconds the receiver's OLED page 2 (or `/status`) should show `RX <chipid>` and a loss percentage, and the CPM number should track the producer's.

### Not working?

Quick things to check:

- **Same WiFi network and subnet.** Multicast doesn't cross routers or guest networks; both devices need to be on the same AP / SSID.
- **Producer in Mode 2.** Mode 1 only sends stats every 30 s and produces no clicks. The CPM on the receiver stays at 0.
- **Receiver actually flashed as `*_udp`.** Check `/info` on the receiver shows a UDP build target. A regular pulse build with a blank input won't listen.
- **Loss percentage stuck high or RX line blank.** Common on busy networks - try `udprx_rxmode = 1` (modem sleep, default) before `2` (none). `2` is usually *worse* on noisy LANs, not better.
- **Wrong producer locked.** In auto-mode (`udprx_chipid` blank), the receiver latches onto the first producer heard. Set the chipid explicitly if you have multiple producers.
- **Stats arriving but no clicks.** The producer just rebooted - mode 2 sends `/click` only for new events. Wait for activity.

## Build Targets

| Target | Platform | Display |
|---|---|---|
| `esp8266_udp` | ESP8266 | headless |
| `esp8266oled_udp` | ESP8266 | SSD1306/SSD1309/SH1106 OLED |
| `esp32_udp` | ESP32 | headless |
| `esp32oled_udp` | ESP32 | OLED |

## Source Modes

Configured on the **Config → Input** page (`udprx_mode`).

| Mode | Behaviour |
|---|---|
| `0` (specific) | If `udprx_chipid` is set, accept clicks only from that producer. If blank (default), auto-latch onto the first producer heard and lock to it. |
| `1` (sum) | Aggregate clicks from every producer heard. Up to 8 tracked. |

The producer chipid is the 6-hex device ID printed on its serial console and visible in its `/info` page. Leave it blank for the common "single producer in the room" case; the receiver will pick the first one it hears.

## RX Sleep Mode (`udprx_rxmode`)

Trade-off between LPS, power, and multicast reliability:

| Mode | ESP8266 | ESP32 | Notes |
|---|---|---|---|
| `0` (light) | `WIFI_LIGHT_SLEEP, LI=1` | `WIFI_PS_MAX_MODEM` | DTIM-aligned wake, CPU naps. Lowest power. |
| `1` (modem, default) | `WIFI_MODEM_SLEEP, LI=1` | `WIFI_PS_MIN_MODEM` | DTIM-aligned wake, CPU stays awake. Best balance. |
| `2` (none) | `WIFI_NONE_SLEEP` | `WIFI_PS_NONE` | Radio always on. Pulls in LAN multicast noise, often *more* loss than modem on busy networks. |

The listen-interval is forced to 1 (every beacon) so AP-buffered multicast is always caught. The factory default of `LI=3` was the cause of "30 CPM producer, 4-15 CPM receiver" observations on early prototypes.

## Gap-Fill Recovery

Every `/click` carries the producer's running counter value and `ts_ms` (producer's `millis()` at emit). The receiver tracks both per producer and detects packet loss as a gap in the sequence.

| Gap size | Action |
|---|---|
| ≤ 1024 clicks | **Instant credit** into the current second's bucket. Typical multicast loss (≤ 1 % at low rates) is fully invisible. |
| 1025 to 65535 clicks | **Rate-drain**: gap divided by its real duration (`ts_ms` delta), then bled into `_local_count` per second at that rate. Long outages and the producer's `MAX_BURST` summary path recover smoothly across CPM history rather than spiking one bucket. |
| > 65535 clicks, or counter going backwards, or invalid duration | **Resync**: logged, no credit. Producer reboot or wildly malformed packets. |

Loss percentage is shown on page 2 of the OLED and exposed via the receiver's accessors. Both instant-credited and drained clicks count toward `_gap_filled`, so `loss_pct` reflects the true fraction of clicks that came from recovery vs. live packets.

## OLED Display

`*oled_udp` builds show a UDP-specific line on page 2: `RX <chipid>` (or `Sum N src` in sum mode), with cumulative loss percentage right-aligned. Page 1 shows the standard CPM / µSv display unchanged, fed from the locally accumulated click count.

# Receive Examples

## python-osc

```python
from pythonosc import dispatcher, osc_server

def on_click(addr, counter, ts_ms):
    print(f"{addr} counter={counter} ts_ms={ts_ms}")

def on_stats(addr, cpm, usv, hv, state, rssi, uptime_s):
    print(f"{addr} {cpm:.1f} CPM {state} up={uptime_s}s")

d = dispatcher.Dispatcher()
d.map("/espg/*/click", on_click)
d.map("/espg/*/stats", on_stats)

server = osc_server.ThreadingOSCUDPServer(
    ("239.255.42.42", 57340), d, multicast=True)
server.serve_forever()
```

## Pure Data / TouchDesigner / Max

Subscribe to multicast group `239.255.42.42` on port `57340`. Filter on path `/espg/*/click` for live click events or `/espg/*/stats` for periodic reports.

# Build Flags

All compile-time tunables (group, port, intervals, bundle size, fleet jitter etc.) are documented in [PlatformIO Build Options → UDP / OSC Output](/install/platformio#udp--osc-output).
