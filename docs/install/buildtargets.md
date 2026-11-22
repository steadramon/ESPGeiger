---
layout: page
title: Build Targets
permalink: /install/buildtargets
nav_order: 20
parent: Install
---

# Build Targets

Builds for both general ESP8266 and ESP32 boards are built automatically by GitHub on Release.

The latest release can always be [found here](https://github.com/steadramon/ESPGeiger/releases/latest).

Builds for a number of target counters are available.

## Pulse

The pulse target is the most widely compatible build and works with all Pulse-type Geiger counters. You'll find a list of compatible and tested pulse counters in the hardware section.

<!-- gen:builds input=pulse skip=espgeiger*,*_hv,xh_s3e_*,esp32s3_audio_* cols=env,chip,rx,oled,pcnt -->

| Build | Chip | RX pin (default) | OLED | Counts with | Notes |
|---|---|---|---|---|---|
| `esp8266_pulse` | ESP8266 | 13 | | interrupt | |
| `esp8266_pulsemin` | ESP8266 | 13 | | interrupt | Minimal build, without MQTT, the uploads and the other outputs |
| `esp8266oled_pulse` | ESP8266 | 13 | yes | interrupt | |
| `esp32_cajoe_iotgm` | ESP32 | 26, wired | yes | PCNT | CAJOE IoT-GM board |
| `esp32_cajoe_iotgm_no_pcnt` | ESP32 | 26, wired | yes | interrupt, PCNT on the Input tab | As `esp32_pulse_no_pcnt`, for the CAJOE IoT-GM |
| `esp32_pulse` | ESP32 | 13 | yes | PCNT | Counts with the built in hardware PCNT counter by default; **Count with** on the Input tab switches to the interrupt counter |
| `esp32_pulse_no_pcnt` | ESP32 | 13 | yes | interrupt, PCNT on the Input tab | The same build defaulting to the interrupt counter. Retired in 0.15: flash the `pulse` build, it keeps the interrupt setting if the 0.14 `_no_pcnt` build ran first. Straight from 0.13 it starts on PCNT: set **Count with** to Interrupt |
| `esp32s3_pulse` | ESP32-S3 | 13 | yes | PCNT | As `esp32_pulse` |
| `esp32s3_pulse_no_pcnt` | ESP32-S3 | 13 | yes | interrupt, PCNT on the Input tab | As `esp32_pulse_no_pcnt` |
| `esp32c3_pulse` | ESP32-C3 | 4 | yes | interrupt | |
| `esp32s2_pulse` | ESP32-S2 | 13 | yes | PCNT | As `esp32_pulse`. No `_no_pcnt` build: switch with **Count with** |

<!-- /gen -->

ESP32 / ESP32-S3 / ESP32-C3 / ESP32-S2 builds bundle SSD1306 OLED support; the panel is auto-detected at boot. No separate `oled_*` envs needed on the ESP32 family. ESP8266 still ships dedicated `oled_*` envs to avoid the runtime cost on the slower core.

## Serial

A single unified serial build supports all serial Geiger counter types. Select the serial protocol on the [Config page](/configuration) under **Input > Serial type**.

<!-- gen:builds input=serial skip=espgeiger*,xh_s3e_*,esp32s3_audio_* cols=env,chip,rx,oled -->

| Build | Chip | RX pin (default) | OLED |
|---|---|---|---|
| `esp8266_serial` | ESP8266 | 13 | |
| `esp8266oled_serial` | ESP8266 | 13 | yes |
| `esp32_serial` | ESP32 | 13 | yes |
| `esp32s3_serial` | ESP32-S3 | 13 | yes |
| `esp32c3_serial` | ESP32-C3 | 4 | yes |
| `esp32s2_serial` | ESP32-S2 | 13 | yes |

<!-- /gen -->

Supported serial types:

<!-- gen:stypes -->

| Type ID | Counter | Baud Rate | Serial Format |
|---|---|---|---|
| 1 | GC10 | 9600 | `60\n` (plain integer) |
| 2 | GC10Next | 115200 | `60\n` (plain integer) |
| 3 | MightyOhm | 9600 | `CPS, 1, CPM, 60, uSv/hr, 1.23, INST\n` |
| 4 | ESPGeiger | 115200 | `CPM: 60\n` |
| 5 | Template | 115200 | Any line: the number after `CPM`, else the first number |

<!-- /gen -->

## Hardware Specific Builds

A number of hardware specific builds are also made to support ESPGeiger-HW and ESPGeiger Log devices.

<!-- gen:builds only=espgeigerhw,espgeigerlog,espgeigerlog_serial,espgeigerlog_udp cols=env,input,rx -->

| Build | Hardware | Input | Notes | RX pin (default) |
|---|---|---|---|---|
| `espgeigerhw` | ESPGeiger-HW | Pulse | Build for the ESPGeiger-HW Geiger Counter. Controls PWM for HV along with other specifics for hardware. | 13, wired |
| `espgeigerlog` | ESPGeiger Log | Pulse | A pulse based build for the ESPGeiger Log hardware with NeoPixel and SDCard output. | 5, wired |
| `espgeigerlog_serial` | ESPGeiger Log | Serial | A serial based build for the ESPGeiger Log hardware. Select your counter type from the Config page. | 5, wired |
| `espgeigerlog_udp` | ESPGeiger Log | UDP receiver | A UDP-receiver build for the ESPGeiger Log hardware. Logs clicks from any ESPGeiger device broadcasting `UdpBlip` on the LAN; useful as a headless aggregator that records to SD card. | none |

<!-- /gen -->

## HV Builds

Generic HV builds enable the same closed-loop high-voltage management used on ESPGeiger-HW, but on stock ESP8266 / ESP32 boards. You wire up your own boost converter (transistor + inductor + voltage divider) and configure the PWM pin at runtime via the **Config** page. See [High Voltage Configuration](/configuration/hv) for wiring and tuning.

<!-- gen:builds hv skip=espgeiger* cols=env,chip,oled -->

| Build | Chip | OLED | Notes |
|---|---|---|---|
| `esp8266_hv` | ESP8266 | | Pulse build with HV generator + ADC feedback. PWM frequency up to 40 kHz; PWM pin defaults to **-1 (disabled)** until set in the Config page. |
| `esp8266oled_hv` | ESP8266 | yes | As `esp8266_hv` with SSD1306 OLED. |
| `esp32_hv` | ESP32 | yes | Pulse build with HV generator + ADC feedback. PWM frequency up to 78 kHz (hardware LEDC). VFB pin defaults to **GPIO 36** (ADC1, no WiFi conflict). SSD1306 OLED auto-detected at boot. |

<!-- /gen -->

For safety the PWM pin defaults to `-1`, so a freshly flashed board drives no pin until you choose one. Set **HV PWM pin** on the [Config page](/configuration) (System tab, **HV Hardware**), then **restart**.

## Audio Tick (ESP32-S3 only)

Builds with the [Audio Tick](/output/audiotick) output enabled. A per-pulse
click is played through an I2S amplifier (e.g. MAX98357A). The XH-S3E set
targets the **MINI ESP32-S3-N16R8** (sold as "XH-S3E-AI", a small purple
ESP32-S3 board with onboard speaker, mic, button, NeoPixel and OLED);
the `esp32s3_audio_*` set targets a generic ESP32-S3 dev kit with an
externally wired amplifier (BCLK / WS / DOUT set on the Config page,
**Output > Audio Tick**). Audio is S3-only: the extra SRAM and headroom
keep speech synthesis and the click engine clear of the counting path.

<!-- gen:builds audio cols=env,input,rx -->

| Build | Hardware | Input | Notes | RX pin (default) |
|---|---|---|---|---|
| `esp32s3_audio_pulse` | ESP32-S3 dev kit + amp | Pulse | Generic S3 board, externally wired I2S amplifier. | 13 |
| `esp32s3_audio_serial` | ESP32-S3 dev kit + amp | Serial | | 13 |
| `xh_s3e_pulse` | MINI ESP32-S3-N16R8 (XH-S3E-AI) | Pulse | Pulse input on GPIO 13. Board pins fixed, the counter RX pin can be changed on Config. | 13 |
| `xh_s3e_serial` | MINI ESP32-S3-N16R8 (XH-S3E-AI) | Serial | Serial input on GPIO 13/14. | 13 |
| `xh_s3e_test` | MINI ESP32-S3-N16R8 (XH-S3E-AI) | Test | Poisson simulator for tuning the click sound. | none |
| `xh_s3e_udp` | MINI ESP32-S3-N16R8 (XH-S3E-AI) | UDP receiver | Tubeless receiver, see UDP Receiver below. | none |

<!-- /gen -->

Audio Tick is on by default. On the `esp32s3_audio_*` envs, set the
three I2S pins on the Config page to match your wiring, then restart.

## UDP Receiver

UDP receiver builds turn an ESP into a tubeless "mirror" device that listens for [Local Broadcast](/output/udp) traffic from another ESPGeiger and behaves as if a real Geiger tube were attached. CPM/µSv/history/blip-LED/MQTT/WebAPI/OLED all derive from the received clicks. See [UDP / OSC Output](/output/udp) for the full protocol and configuration.

<!-- gen:builds input=udprx skip=espgeiger*,xh_s3e_* cols=env,chip,oled -->

| Build | Chip | OLED | Notes |
|---|---|---|---|
| `esp8266_udp` | ESP8266 | | Headless receiver. |
| `esp8266oled_udp` | ESP8266 | yes | Receiver with display; shows producer chipid + loss% on page 2 and a feed-alive indicator on page 1. |
| `esp32_udp` | ESP32 | yes | ESP32 receiver. SSD1306 OLED auto-detected at boot; more heap headroom than ESP8266, cleaner choice for fleet aggregator (sum mode). |
| `esp32s3_udp` | ESP32-S3 | yes | ESP32-S3 receiver with bundled OLED. |
| `esp32c3_udp` | ESP32-C3 | yes | ESP32-C3 receiver with bundled OLED. |
| `esp32s2_udp` | ESP32-S2 | yes | ESP32-S2 receiver with bundled OLED. |

<!-- /gen -->

After flashing, set **Source mode** on **Config > Input**: Specific device (leave Source chip ID blank to latch the first source heard) or Sum all sources. Group / listen IP and UDP port are under advanced.

## Test and Custom Builds

Test builds simulate a Geiger counter and never upload to public services. They are described in [Testing & Development](/dev#test-builds). To build your own firmware, see [Platform.io Build](/dev/platformio).
