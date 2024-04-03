---
layout: page
title: Build Targets
permalink: /install/buildtargets
nav_order: 2
parent: Install
---

# Build Targets

Builds for both general ESP8266 and ESP32 boards are built automatically by GitHub on Release.

The latest release can always be [found here](https://github.com/steadramon/ESPGeiger/releases/latest).

Builds for a number of target counters are available.

## Pulse

The pulse Geiger counter target is the widest compatible target. This is used for all Pulse type Geiger counters. A list of compatible and tested Pulse counters cant be found under the hardware section.

| Target Name | Target Counter | Notes |
|---|---|---|
`pulse` | Pulse | ESP32 builds default to using built in hardware PCNT counter
`pulse_no_pcnt` | Pulse | ESP32-only build disabling PCNT

## Serial

Numerous serial Geiger target builds are made, one for each of the following target serial counters -

| Target Name | Target Counter | Notes |
|---|---|---|
`gc10` | GC10 | 
`gc10next` | GC10-next | 
`mightyohm` | Mighty Ohm |

## Hardware Specific Builds

A number of hardware specific builds are also made to support ESPGeiger-HW and ESPGeiger Log devices.

| Target Name | Target Counter | Counter Type | Notes |
|---|---|---|---|
`espgeigerhw` | ESPGeiger‑HW | Pulse | Build for the ESPGeiger-HW Geiger Counter. Controls PWM for HV along with other specifics for hardware.
`espgeigerlog` | ESPGeiger Log | Pulse | A pulse based build for the ESPGeiger Log hardware with NeoPixel and SDCard output.
`espgeigerlog_gc10` | ESPGeiger Log | Serial (GC10) | A GC10 serial based build for the ESPGeiger Log hardware with NeoPixel and SDCard output.
`espgeigerlog_gc10next` | ESPGeiger Log | Serial (GC10next) | A GC10next serial based build for the ESPGeiger Log hardware with NeoPixel and SDCard output.
`espgeigerlog_mightyohm` | ESPGeiger Log | Serial (Mighty Ohm) | A Mighty Ohm serial based build for the ESPGeiger Log hardware with NeoPixel and SDCard output.

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
`testgc10` | GC10 | Test build which outputs an emulated GC10 serial based Geiger counter on the TXPIN
`testmightyohm` | Mighty Ohm | Test build which outputs an emulated Mighty Ohm serial based Geiger counter on the TXPIN

### Test Build Options

## Hardware Specific Test Builds

| Target Name | Target Counter | Counter Type | Notes |
|---|---|---|---|
`espgeigerlog_test` | ESPGeiger Log | n/a | A test build for the ESPGeiger Log hardware. Internal interrupt based counter. No output, mostly for testing the ESPGeiger Log functionality.
`espgeigerlog_testpulse` | ESPGeiger Log | Pulse | A test build for the ESPGeiger Log hardware. Test build which outputs a pulse on the PLS pin.