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

The pulse target is the most widely compatible build, and works with all Pulse-type Geiger counters. You'll find a list of compatible and tested pulse counters in the hardware section.

| Target Name | Target Counter | Notes |
|---|---|---|
`pulse` | Pulse | ESP32 builds default to using built in hardware PCNT counter
`pulse_no_pcnt` | Pulse | ESP32-only build disabling PCNT

## Serial

A single unified serial build supports all serial Geiger counter types. The serial protocol is selected at runtime via the **Config** page (System > Serial Type).

| Target Name | Notes |
|---|---|
`serial` | Unified serial build. Supports GC10, GC10Next, MightyOhm and ESPGeiger protocols. Select your counter type from the Config page after flashing.

Supported serial types:

| Type ID | Counter | Baud Rate | Serial Format |
|---|---|---|---|
1 | GC10 | 9600 | `60\n` (plain integer)
2 | GC10Next | 115200 | `60\n` (plain integer)
3 | MightyOhm | 9600 | `CPS, 1, CPM, 60, uSv/hr, 1.23, INST\n`
4 | ESPGeiger | 115200 | `CPM: 60\n`

## Hardware Specific Builds

A number of hardware specific builds are also made to support ESPGeiger-HW and ESPGeiger Log devices.

| Target Name | Target Counter | Counter Type | Notes |
|---|---|---|---|
`espgeigerhw` | ESPGeiger-HW | Pulse | Build for the ESPGeiger-HW Geiger Counter. Controls PWM for HV along with other specifics for hardware.
`espgeigerlog` | ESPGeiger Log | Pulse | A pulse based build for the ESPGeiger Log hardware with NeoPixel and SDCard output.
`espgeigerlog_serial` | ESPGeiger Log | Serial | A serial based build for the ESPGeiger Log hardware. Select your counter type from the Config page.
`espgeigerlog_udp` | ESPGeiger Log | UDP receiver | A UDP-receiver build for the ESPGeiger Log hardware. Logs clicks from any ESPGeiger device broadcasting `UdpBlip` on the LAN; useful as a headless aggregator that records to SD card.

## HV Builds

Generic HV builds enable the same closed-loop high-voltage management used on ESPGeiger-HW, but on stock ESP8266 / ESP32 boards. You wire up your own boost converter (transistor + inductor + voltage divider) and configure the PWM pin at runtime via the **Config** page. See [High Voltage Configuration](/configuration/hv) for wiring and tuning.

| Target Name | Platform | Notes |
|---|---|---|
`esp8266_hv` | ESP8266 | Pulse build with HV generator + ADC feedback. PWM frequency up to 40 kHz; PWM pin defaults to **-1 (disabled)** until set in the Config page.
`esp32_hv` | ESP32 | Pulse build with HV generator + ADC feedback. PWM frequency up to 80 kHz (hardware LEDC). VFB pin defaults to **GPIO 36** (ADC1, no WiFi conflict).

For safety the PWM pin defaults to `-1` so freshly flashed firmware does **not** drive any pin until you've explicitly chosen one. After setting the pin in `/egprefs`, **reboot** for the change to take effect.

## UDP Receiver

UDP receiver builds turn an ESP into a tubeless "mirror" device that listens for [Local Broadcast](/output/udp) traffic from another ESPGeiger and behaves as if a real Geiger tube were attached. CPM/µSv/history/blip-LED/MQTT/WebAPI/OLED all derive from the received clicks. See [UDP / OSC Output](/output/udp) for the full protocol and configuration.

| Target Name | Platform | Display | Notes |
|---|---|---|---|
`esp8266_udp` | ESP8266 | none | Headless receiver. |
`esp8266oled_udp` | ESP8266 | OLED | Receiver with display; shows producer chipid + loss% on page 2 and a feed-alive indicator on page 1. |
`esp32_udp` | ESP32 | none | ESP32 headless receiver. More heap headroom; cleaner choice for fleet aggregator (sum mode). |
`esp32oled_udp` | ESP32 | OLED | ESP32 receiver with display. |

After flashing, configure the source mode (pin / sum / auto) and group/port from the **Config → Input** page.

## Test

Test builds can be used to emulate a Geiger counter with your board. You can connect the ESPGeiger `RXPIN` and `TXPIN` together, or connect one ESPGeiger `TXPIN` to the `RXPIN` on another ESPGeiger.

_Note_: No values are submitted to public services with Test builds.

By default the Test output cycles through several ranges of reading, switching each 5 minute period:

- 0.5 CPS / 30 CPM
- 1 CPS / 60 CPM
- 1.66 CPS / 100 CPM
- 2 CPS / 120 CPM

| Target Name | Target Counter | Notes |
|---|---|---|
`test` | n/a | Internal interrupt based counter. No output, mostly for testing the `Counter.h` functionality.
`testpulse` | Pulse | Test build which outputs a Poisson distributed pulse on the TXPIN
`testpulsepwm` | Pulse | Test build which outputs a interrupt generated pulse on the TXPIN
`testserial` | Serial | Test build which outputs emulated serial data on the TXPIN. Serial format selectable at runtime via Config page.

### Test Build Options

## Hardware Specific Test Builds

| Target Name | Target Counter | Counter Type | Notes |
|---|---|---|---|
`espgeigerlog_test` | ESPGeiger Log | n/a | A test build for the ESPGeiger Log hardware. Internal interrupt based counter. No output, mostly for testing the ESPGeiger Log functionality.
`espgeigerlog_testpulse` | ESPGeiger Log | Pulse | A test build for the ESPGeiger Log hardware. Test build which outputs a pulse on the PLS pin.
