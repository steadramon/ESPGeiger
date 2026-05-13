---
layout: page
title: UDP / OSC
permalink: /output/udp
parent: Outputs
nav_order: 10
---

# UDP Blip Out

ESPGeiger can broadcast click events and periodic stats to a UDP multicast group using the [Open Sound Control (OSC) 1.0](https://opensoundcontrol.stanford.edu/spec-1_0.html) wire format. Any OSC-aware tool on the same LAN (Pure Data, TouchDesigner, python-osc, Max/MSP, Node-RED) can subscribe and react to live radiation events without polling MQTT or HTTP.

A matching receiver firmware variant (`UDP-Receiver`) lets a tubeless ESP device mirror another ESPGeiger over the air: display, MQTT, WebAPI, OLED and blip-LED all work as if a real tube were attached.

- Multicast group: `239.255.42.42` (site-local, configurable)
- UDP port: `57340` (configurable)
- Service announced via mDNS as `_osc._udp`

## Output Modes

Configured on the **Config → UDP blip out** page.

| Mode | Behaviour |
|---|---|
| `0` (off) | No packets sent. Default. |
| `1` (stats only) | Emits `/stats` every 60 s. No per-click traffic. |
| `2` (stats + blips) | `/stats` plus one `/click` per radiation event, OSC-bundled when bursts ≥ 2 clicks. |

## OSC Message Schema

### `/espg/<chipid>/click`

Emitted on every accepted click in mode 2.

```
,iif
  counter : int32   monotonic click number on the producer
  ts_ms   : int32   producer millis() at emit
  cps     : float   counts-per-second snapshot
```

### `/espg/<chipid>/stats`

Periodic heartbeat (60 s ± random per-boot jitter, see "Stats Jitter" below).

```
,fffsi
  cpm   : float    1-minute average
  usv   : float    µSv/h
  hv    : float    HV reading on HW builds, else 0
  state : string   "warming" | "healthy" | "warning" | "alert"
  rssi  : int32    WiFi signal strength
```

### OSC Bundles

When ≥ 2 clicks fire in the same loop pass, `/click` messages are wrapped in an OSC `#bundle` (up to 10 per packet, `UDPBLIP_BUNDLE_MAX`) to cut WiFi airtime. Receivers unpack bundles transparently; every reasonable OSC library dispatches a callback per inner message.

For sustained hot sources above `UDPBLIP_MAX_BURST=200` clicks/loop (~4 000 cps, ~240 000 CPM ceiling), the producer collapses the burst into a single summary event rather than fragmenting across many packets.

## Stats Jitter

The first `/stats` emission is offset by a per-device `GRNG`-seeded random delay (default `UDPBLIP_STATS_JITTER_MS=15000` ms). A fleet of 100 devices booting together after a power-cut spreads its stats packets across the 15 s window, averaging 150 ms between emissions, well clear of the receiver's lwIP UDP mbox (6-deep on ESP8266).

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

`*oled_udp` builds show two UDP-specific indicators:

- **Page 1, bottom-right corner of the graph**: filled-dot glyph while we've heard any packet (click *or* stats heartbeat) in the last `UDPRX_STALE_MS` (default 70 s). Warning glyph replaces it when nothing has been heard in that window. Refreshes once per second.
- **Page 2**: Line `RX <chipid>` (or `Sum N src` in sum mode), with cumulative loss percentage right-aligned.

The 70 s threshold is just over one `/stats` heartbeat interval, so a quiet click feed never false-triggers stale as long as the producer's stats heartbeat is arriving.

# Receive Examples

## python-osc

```python
from pythonosc import dispatcher, osc_server

def on_click(addr, counter, ts_ms, cps):
    print(f"{addr} counter={counter} cps={cps:.2f}")

def on_stats(addr, cpm, usv, hv, state, rssi):
    print(f"{addr} {cpm:.1f} CPM {state}")

d = dispatcher.Dispatcher()
d.map("/espg/*/click", on_click)
d.map("/espg/*/stats", on_stats)

server = osc_server.ThreadingOSCUDPServer(
    ("239.255.42.42", 57340), d, multicast=True)
server.serve_forever()
```

## Pure Data / TouchDesigner / Max

Subscribe to multicast group `239.255.42.42` on port `57340`. Filter on path `/espg/*/click` for live click events or `/espg/*/stats` for periodic reports.

# Tuning Constants

Override at compile time via `-D`:

| Macro | Default | Effect |
|---|---|---|
| `UDPBLIP_STATS_INTERVAL_MS` | 60000 | Interval between `/stats` heartbeats |
| `UDPBLIP_STATS_JITTER_MS` | 1000 | First-emission GRNG offset range |
| `UDPBLIP_BUNDLE_MAX` | 10 | Max `/click` messages per OSC bundle |
| `UDPBLIP_MAX_BURST` | 200 | Per-loop click cap before summary path |
| `UDPBLIP_FAIL_BACKOFF` | 8 | Consecutive send failures before cool-off |
| `UDPRX_PRODUCER_SLOTS` | 8 | Producer tracking table size (sum mode) |
| `UDPRX_STALE_MS` | 70000 | OLED stale-feed threshold (any packet) |
| `UDPRX_DEFAULT_GROUP` | `"239.255.42.42"` | Default multicast group |
| `UDPRX_DEFAULT_PORT` | `"57340"` | Default UDP port |
