[![Build](https://github.com/steadramon/ESPGeiger/workflows/Build/badge.svg?branch=main)](https://github.com/steadramon/ESPGeiger/actions) [![Issues](https://img.shields.io/github/issues/steadramon/ESPGeiger)](https://github.com/steadramon/ESPGeiger/issues) [![Stars](https://img.shields.io/github/stars/steadramon/ESPGeiger)](https://github.com/steadramon/ESPGeiger/stargazers)

# <img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/favicon.svg" width="30px"/> ESPGeiger

ESPGeiger is an open-source project that turns an ESP8266 or ESP32 into an IoT-connected Geiger counter. It collects, calculates and reports radiation levels from a range of Geiger counters with pulse or serial outputs.

- 😃  Easy to install via web browser - no compiler required
- 📈  Built-in web portal with live graphing and 24-hour history
- 🔴  Live CPM and μSv/h with 1, 5 and 15-minute smoothed averages
- 🕰️   Lifetime tracking - persistent total clicks and μSv across reboots
- 🖥️   Optional SSD1306 / SSD1309 / SH1106 OLED and WS2812X NeoPixel status light
- 🌡️   Optional BME280 / BMP280 / AHT environmental sensors - auto-detected over I2C
- ✅  Works with pulse and GC10 / GC10next / MightyOhm serial counters
- 📟  Accurate counting via interrupt or ESP32 hardware counter (PCNT)
- 🎛️   Configurable noise filtering and debounce
- 🌐  MQTT and Home Assistant auto-discovery, Radmon.org, GMCMAP, ThingSpeak, GeigerLog, custom Webhooks
- 📡  UDP / OSC broadcast and receiver builds - mirror clicks to a tubeless ESP, or feed PD / TouchDesigner / Node-RED
- 💾  Optional SD card logging
- 🗂️   Portable config backup and restore - transferable between devices
- 🚧  Test builds for emulating pulse and serial counters

<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/hardware/img/ESPGeigerLog.png" width="50%"/>
<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/statuspage.png" width="50%"/>

## Quick Start

The easiest way to install ESPGeiger is the **Web Installer** - it runs in your Chrome or Edge browser and flashes your device over USB. No code editor or compiler required.

### 👉 [https://install.espgeiger.com](https://install.espgeiger.com)

Select your build from the dropdown and follow the on-screen instructions. Installation takes about two minutes.

## Which Build Do I Need?

Pick the build that matches your hardware. If in doubt, start with **Pulse** - it works with most generic Geiger counters.

### I have an ESP + generic pulse Geiger counter

| Hardware | Recommended Build |
|---|---|
| ESP8266 (Wemos D1 Mini, NodeMCU, etc.) | `esp8266_pulse` |
| ESP8266 with SSD1306 OLED | `esp8266oled_pulse` |
| ESP32 | `esp32_pulse` |
| ESP32-S3 (dev kits) | `esp32s3_pulse` |
| ESP32-S3 with onboard amp + speaker (XH-S3E-AI) | `xh_s3e_pulse` |
| ESP32-C3 (RISC-V, no PCNT - software pulse counter) | `esp32c3_pulse` |
| CAJOE IoT-GM | `esp32_cajoe_iotgm` |

ESP32 / ESP32-S3 / ESP32-C3 builds bundle SSD1306 OLED support and auto-detect the panel at boot - no separate `oled_*` envs on the ESP32 family. ESP8266 still ships dedicated `esp8266oled_*` envs to avoid the runtime cost on the slower core.

### I have an ESP + serial Geiger counter

A single serial build supports all serial counter types (GC10, GC10Next, MightyOhm, ESPGeiger). Select your counter type from the **Config** page after flashing.

| ESP8266 Build | ESP32 Build | ESP32-S3 Build | ESP32-C3 Build |
|---|---|---|---|
| `esp8266_serial` | `esp32_serial` | `esp32s3_serial` | `esp32c3_serial` |

ESP8266 OLED variant: `esp8266oled_serial`. The ESP32 family bundles OLED so the same `esp32*_serial` envs work with or without a display.

### I have an ESPGeiger-HW or ESPGeiger Log

These are official ESPGeiger hardware kits - use the hardware-specific builds:

| Hardware | Pulse | Serial |
|---|---|---|
| ESPGeiger-HW | `espgeigerhw` | - |
| ESPGeiger Log | `espgeigerlog` | `espgeigerlog_serial` |

### I want to test without a real Geiger counter

Test builds emulate a Geiger counter internally. You can also wire the `TXPIN` of one ESPGeiger to the `RXPIN` of another to simulate a serial counter:

| Build | Description |
|---|---|
| `esp8266_test` / `esp32_test` / `esp32s3_test` | Internal counter, no output |
| `esp8266_testpulse` / `esp32_testpulse` | Outputs Poisson-distributed pulses on TXPIN |
| `esp8266_testserial` / `esp32_testserial` | Emulates a serial counter (type selectable via Config) |

### I want a remote display or fleet aggregator (no tube needed)

UDP receiver builds listen for OSC multicast broadcasts from other ESPGeiger devices and mirror them locally. Everything (CPM, µSv, OLED, MQTT, blip-LED) works as if a real tube were attached:

| Hardware | Build |
|---|---|
| ESP8266 | `esp8266_udp` / `esp8266oled_udp` |
| ESP32 | `esp32_udp` |
| ESP32-C3 | `esp32c3_udp` |
| ESP32-S3 | `esp32s3_udp` |

Producers enable the broadcast via Config → Local broadcast → mode 2. See [UDP / OSC Output](https://docs.espgeiger.com/output/udp) for the full protocol.

See the full list of available builds on the [releases page](https://github.com/steadramon/ESPGeiger/releases/latest) or in the [build targets documentation](https://docs.espgeiger.com/install/buildtargets).

## Hardware Connection

By default `GEIGER_RXPIN` is set to GPIO13 on ESP8266 / ESP32, and GPIO4 on ESP32-C3 (GPIO 11-17 are bonded to internal flash on C3-MINI-1 modules and cannot be used). Connect your Geiger counter's pulse or serial TX output to this pin. Don't forget a common ground.

<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/D1mini-basicwiring.png" width="50%"/>
<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/cajoe-wemosd1.jpg" width="50%"/>

## First-time Setup

1. After flashing, connect to the new Wi-Fi network (`ESPGeiger-XXXXXX`)
2. A captive portal should pop up - if not, browse to http://192.168.4.1/
3. Select your home Wi-Fi network and enter the password
4. Once connected, browse to `http://ESPGeiger-XXXXXX.local` or the assigned IP
5. Configure MQTT, Radmon, GMCMAP, ThingSpeak and other outputs from the Config page

> 🔒 ESPGeiger is designed for LAN use. Don't port-forward it to the open internet - use a VPN for remote access, and push telemetry outward (MQTT / Radmon / ThingSpeak / Webhook) for remote monitoring. See [Network and security](https://docs.espgeiger.com/install/setup#network-and-security).

<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/ESPGeiger-anim.gif" width="75%"/>

## Upgrading

Upgrades can be done over-the-air through the web interface. Please be careful to select the right firmware for your hardware.

Note: ESP32 users should use the `firmware` binary when upgrading (the `merged` binaries are for fresh installs via the Web Installer).

## Documentation

Full documentation is available at **[docs.espgeiger.com](https://docs.espgeiger.com/)**.

## Building from Source

If you want to customise the firmware or create a build for unsupported hardware, you can build from source using PlatformIO. See the [Platformio Build documentation](https://docs.espgeiger.com/install/platformio) for details.

## Compatible Geiger Counters

### Pulse

- [DIY GeigerKit](https://sites.google.com/site/diygeigercounter/)
- [NetIO GC10](https://www.ebay.co.uk/usr/pelorymate)
- [RHElectronics](https://www.rhelectronics.store/diy-geiger-counter-kit)
- [GeigerHV](https://www.ebay.co.uk/usr/geigerhv)
- [GGreg20](https://www.tindie.com/stores/iotdev/)
- [MightyOhm Kit](https://www.tindie.com/stores/mightyohm/) (can also be used as a pulse counter)
- [DiY-GDC](https://www.ebay.com/usr/impexeris)
- CAJOE (and clones) RadiationD-v1.1

### Serial

- GC10 / GC10-Next
- MightyOhm
- ESPGeiger-HW

Other serial-based counters should be supportable with small additions to the codebase. If you have one not listed, please [raise an issue](https://github.com/steadramon/ESPGeiger/issues).

## Announcing ESPGeiger-HW

<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/ESPGeiger-HW-STS-5.jpg" width="75%"/>

The official IoT Geiger counter powered by ESPGeiger. See [docs.espgeiger.com](https://docs.espgeiger.com/hardware/espgeigerhw) for more information.

## Contributions

Contributions are welcome - please feel free to raise a Pull Request or open an issue.

## Thanks 🙏

Supporting libraries:
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) / [ESPAsyncTCP](https://github.com/ESP32Async/ESPAsyncTCP)
- [async-mqtt-client](https://github.com/marvinroger/async-mqtt-client)
- [AsyncHTTPRequest_Generic](https://github.com/khoih-prog/AsyncHTTPRequest_Generic)
- [U8g2](https://github.com/olikraus/u8g2)
- [JLed](https://github.com/jandelgado/jled)
- [NeoPixelBus](https://github.com/Makuna/NeoPixelBus)
- [EspSoftwareSerial](https://github.com/plerup/espsoftwareserial)
- [SdFat](https://github.com/greiman/SdFat)
- [micro-ecc](https://github.com/kmackay/micro-ecc)
- [CircularBuffer](https://github.com/rlogiacco/CircularBuffer)
- [base64_arduino](https://github.com/Densaugeo/base64_arduino) (basis for the bundled EGBase64)
- [ESPNtpClient](https://github.com/gmag11/ESPNtpClient) (uptime tracking, now inlined)

Inspiration:
- [OpenMQTTGateway](https://github.com/1technophile/OpenMQTTGateway)
- [tinyGS](https://github.com/G4lile0/tinyGS/)
- [FreqCountESP](https://github.com/kapraran/FreqCountESP)

With thanks to:
- [Mr Blinky](https://www.blinkyslab.co.uk/) - feedback, inspiration and support
- [Jander](https://r.jander.me.uk/) - tall robot guy
