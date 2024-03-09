---
layout: page
title: Install
permalink: /install
nav_order: 1
has_children: true
---

# Install

The easiest way to install is to visit the web based installed in your Chrome or Edge browser:

https://install.espgeiger.com

# Build Targets

Builds for both ESP8266 and ESP32 are built automatically by github on release.

Builds for a number of target counters are available.

## Pulse

The pulse Geiger counter target is the widest compatible target. This is used for all Pulse type Geiger counters. A list of compatible and tested Pulse counters cant be found under the hardware section.

| Target Name | Target Counter | Notes |
|---|---|---|
`pulse` | Pulse | 

## Serial

Numerous serial Geiger target builds are made, one for each of the following target serial counters -

| Target Name | Target Counter | Notes |
|---|---|---|
`gc10` | GC10 | 
`gc10next` | GC10-next | 
`mightyohm` | Mighty Ohm |

## Hardware

| Target Name | Target Counter | Counter Type | Notes |
|---|---|---|---|
`espgeigerhw` | ESPGeiger-HW | Pulse | Build for the ESPGeiger-HW Geiger Counter. Controls PWM for HV along with other specifics for hardware.
`espgeigerlog` | ESPGeiger-Log | Pulse | A pulse based build for the ESPGeiger-Log hardware with NeoPixel and SDCard output.
`espgeigerloggc10` | ESPGeiger-Log | GC10 | A GC10 serial based build for the ESPGeiger-Log hardware with NeoPixel and SDCard output.
`espgeigerloggc10next` | ESPGeiger-Log | GC10next | A GC10next serial based build for the ESPGeiger-Log hardware with NeoPixel and SDCard output.
`espgeigerloggc10mightyohm` | ESPGeiger-Log | Mighty Ohm | A Mighty Ohm serial based build for the ESPGeiger-Log hardware with NeoPixel and SDCard output.

## Test

Test builds can be used to emulate a Geiger counter with your board. You can connect the ESPGeiger RXPIN and TXPIN together, or connect one ESPGeiger TXPIN to the RXPIN on another.
 
| Target Name | Target Counter | Notes |
|---|---|---|
`test` | n/a | Internal interrupt based counter. No output, mostly for testing the Counter.h functionality.
`testpulse` | Pulse | Test build which outputs a pulse on the TXPIN
`testgc10` | GC10 | Test build which outputs an emulated GC10 serial based Geiger counter on the TXPIN
`testmightyohm` | Mighty Ohm | Test build which outputs an emulated Mighty Ohm serial based Geiger counter on the TXPIN

## Hardware Test

| Target Name | Target Counter | Counter Type | Notes |
|---|---|---|---|
`espgeigerlog_test` | ESPGeiger-Log | n/a | A test build for the ESPGeiger-Log hardware. Internal interrupt based counter. No output, mostly for testing the ESPGeiger-Log functionality.
`espgeigerlog_testpulse` | ESPGeiger-Log | Pulse | A test build for the ESPGeiger-Log hardware. Test build which outputs a pulse on the PLS pin.