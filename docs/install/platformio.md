---
layout: page
title: Platform.io Build
permalink: /install/platformio
nav_order: 40
parent: Install
---

# Platformio Custom Build

- Platform.io is the development and build SDK for ESPGeiger.
- Platform.io is configured to install any requirements and build firmware for each of the build targets.
- Builds of configured releases are done automatically by GitHub Actions.

## Build Options

The following build flags can be added to your environment's `build_flags` to customise behaviour.

### Output Modules

| Flag | Description |
|---|---|
| `-D MQTTOUT` | Enable MQTT output. |
| `-D MQTTAUTODISCOVER` | Enable Home Assistant MQTT auto-discovery. Requires `MQTTOUT`. |
| `-D RADMONOUT` | Enable Radmon.org submission. |
| `-D GMCOUT` | Enable GMC World Map submission. |
| `-D THINGSPEAKOUT` | Enable ThingSpeak submission. |
| `-D WEBHOOKOUT` | Enable custom Webhook output. |
| `-D SERIALOUT` | Enable serial CPM/uSv output. |
| `-D GEIGER_SDCARD` | Enable SD card data logging. |
| `-D SDCARD_EXTENDEDLOG` | Add extra columns (free memory) to the SD card CSV output. Requires `GEIGER_SDCARD`. |

### Display & LEDs

| Flag | Description |
|---|---|
| `-D SSD1306_DISPLAY` | Enable SSD1306 OLED display. |
| `-D OLED_SDA=N` | Set the OLED I2C SDA pin (default `4`). |
| `-D OLED_SCL=N` | Set the OLED I2C SCL pin (default `5`). |
| `-D OLED_FLIP=true` | Flip the OLED display vertically. |
| `-D GEIGER_NEOPIXEL` | Enable WS2812X NeoPixel status LED. |
| `-D NEOPIXEL_PIN=N` | Set the NeoPixel data pin (default `15`). |
| `-D GEIGER_PUSHBUTTON` | Enable push button for OLED page switching and display wake. |
| `-D PUSHBUTTON_PIN=N` | Set the push button pin (default `14`). |
| `-D LED_SEND_RECEIVE=N` | Set the status LED pin (default `2`). |

### Geiger Counter Type

The geiger counter type and serial type are defined in `platformio.ini` for use in environment configurations:

```ini
[geiger_type]
pulse = 1
serial = 2
test = 3
testpulse = 4
testserial = 5
testpulseint = 6

[serial_type]
gc10 = 1
gc10nx = 2
mightyohm = 3
espgeiger = 4
```

These can be referenced in your environment's `build_flags` as:

```ini
build_flags =
  -DGEIGER_TYPE=${geiger_type.pulse}
  -DGEIGER_SERIALTYPE=${serial_type.gc10}
```

| Flag | Description |
|---|---|
| `-D GEIGER_TYPE=N` | Set the geiger counter type (bitfield — bit 0 = pulse, bit 1 = serial, bit 2 = internal PWM, bit 7 = test). `1` Pulse (default), `2` Serial, `128` Test, `129` Test Pulse, `130` Test Serial, `133` Test Pulse PWM. Values ≥ 128 are test builds. |
| `-D GEIGER_SERIALTYPE=N` | Set the **default** serial counter type for serial builds. The serial type can be changed at runtime from the Config page. `1` GC10 (9600 baud), `2` GC10-Next (115200 baud), `3` MightyOhm (9600 baud), `4` ESPGeiger (115200 baud). |

### Geiger Counter Input

| Flag | Description |
|---|---|
| `-D GEIGER_RXPIN=N` | Set the geiger counter input pin (default `13`). |
| `-D GEIGER_TXPIN=N` | Set the transmit pin for test builds (default `-1`). |
| `-D GEIGER_DEBOUNCE=N` | Set the default pulse debounce time in microseconds (default `500`). This can also be configured at runtime from the web interface on builds that use software interrupt counting. |
| `-D GEIGER_RATIO=N` | Set the default CPM to μSv/h conversion ratio (default `151.0`). |
| `-D GEIGER_BAUDRATE=N` | Set the serial baud rate for serial-type counters. |
| `-D GEIGER_MODEL="name"` | Set the geiger counter model name. |
| `-D RXPIN_BLOCKED` | Prevent the RX pin from being changed via the web interface. |
| `-D TXPIN_BLOCKED` | Prevent the TX pin from being changed via the web interface. |
| `-D DISABLE_SERIALRX` | Disable the serial command interface on the hardware UART. |

### Data Smoothing

By default, the 5-minute and 15-minute CPM values use exponential moving average (EMA) smoothing. This provides smooth, responsive readings with minimal memory usage.

| Flag | Description |
|---|---|
| `-D GEIGER_SMOOTH_AVG` | Use rolling average buckets instead of EMA for the 5 and 15-minute CPM values. Uses more memory but gives equal weighting to all readings in the window. |
| `-D GEIGER_EMA_FACTOR=N` | Set the EMA smoothing factor (default `5`). Higher values are more responsive, lower values are smoother. A value of `3` closely matches the rolling average behaviour. |

The 1-minute CPM value always uses a 60-second rolling average for accuracy.

### ESP32 Hardware Counter (PCNT)

On ESP32, the hardware pulse counter (PCNT) is used by default for pulse-type geiger counters. This provides accurate counting without software interrupts.

| Flag | Description |
|---|---|
| `-D IGNORE_PCNT` | Disable hardware PCNT and use software interrupt counting instead. |
| `-D PCNT_FILTER=N` | Set the initial PCNT glitch filter value at compile time. Filters pulses shorter than N APB clock cycles. This can also be configured at runtime from the web interface. |
| `-D PCNT_FLOATING_PIN` | Disable the internal pull resistor on the PCNT input. Use when the geiger module already provides its own pull. |
| `-D PCNT_PULLDOWN_PIN` | Use an internal pulldown instead of the default pullup. For active-high modules without their own idle pull. |
| `-D OTA_POLL_INTERVAL_MS=N` | How often to poll the ArduinoOTA UDP socket, in ms (default `100`). Shorter = faster OTA start response, more CPU cost. |
| `-D CMAN_PROCESS_INTERVAL_MS=N` | How often to run the config-portal/HTTP handler in the main loop, in ms. Default `10` on ESP32, `5` on ESP8266. Shorter = faster HTTP response, more CPU overhead. |
| `-D MQTT_HASS_REFRESH_S=N` | How often to republish Home Assistant autodiscovery configs on MQTT reconnect, in seconds (default `3600` = 1h). `0` = publish on every reconnect (legacy behaviour). |

### MQTT Options

| Flag | Description |
|---|---|
| `-D MQTT_MEM_DEBUG` | Include additional memory statistics in the MQTT status JSON (free heap, max block, fragmentation). |
| `-D DISABLE_MQTT_CPS` | Disable publishing the CPS topic to reduce MQTT traffic. |

### Test Build Options

| Flag | Description |
|---|---|
| `-D GEIGER_TEST_FIXEDCPM` | Lock the test output to a single CPM value instead of cycling through 30/60/100/120. |
| `-D GEIGER_COUNT_TXPULSE` | Count the pulses generated on the TX pin locally. Note: if RX and TX pins are connected together, counts will be doubled. |

### Hardware Specific

These flags are used by ESPGeiger-HW and ESPGeiger Log specific builds.

| Flag | Description |
|---|---|
| `-D ESPGEIGER_HW` | Enable ESPGeiger-HW hardware support. Enables HV PWM control, voltage feedback monitoring and related configuration. |
| `-D ESPGEIGER_LT` | Identify the build as an ESPGeiger Lite/Log device. |
| `-D GEIGER_PWMPIN=N` | Set the HV PWM output pin (default `12`). Requires `ESPGEIGER_HW`. |
| `-D GEIGER_VFEEDBACKPIN=N` | Set the HV voltage feedback ADC pin (default `A0`). Requires `ESPGEIGER_HW`. |
| `-D GEIGERHW_FREQ=N` | Set the default HV PWM frequency in Hz (default `6000`). Requires `ESPGEIGER_HW`. |
| `-D GEIGERHW_DUTY=N` | Set the default HV PWM duty cycle 0-255 (default `70`). Requires `ESPGEIGER_HW`. |
| `-D GEIGERHW_ADC_RATIO=N` | Set the ADC voltage divider ratio for HV reading (default `1035`). Requires `ESPGEIGER_HW`. |
| `-D GEIGERHW_ADC_OFFSET=N` | Set the ADC offset for HV reading calibration (default `0`). Requires `ESPGEIGER_HW`. |
| `-D GEIGER_BLIPLED=N` | Set the blip LED pin for ESPGeiger-HW (default `15`). Requires `ESPGEIGER_HW`. |
| `-D GEIGER_SDCARD_CS=N` | Set the SD card SPI chip select pin (default `16`). Requires `GEIGER_SDCARD`. |

### Miscellaneous

| Flag | Description |
|---|---|
| `-D DISABLE_BLIP` | Disable the LED blip on each detected pulse. |
| `-D DISABLE_INTERNAL_BLIP` | Disable the internal status LED blip on each detected pulse. |
| `-D NTP_SERVER="server"` | Set the default NTP server (default `pool.ntp.org`). |
| `-D NTP_TZ="tz"` | Set the default timezone in POSIX format. |

### SdFat (SD card builds)

Applied globally in `[com-esp]` so they affect every SD-enabled build:

| Flag | Description |
|---|---|
| `-D MAINTAIN_FREE_CLUSTER_COUNT=1` | Cache free-cluster count; `freeClusterCount()` becomes O(1) after first call. Eliminates multi-second "calculating free space" delay at boot and daily cleanup scan. |
| `-D SDFAT_FILE_TYPE=1` | FAT16/FAT32 only (no exFAT). Saves ~15-20 KB flash; we use FAT32 exclusively. |
| `-D USE_LONG_FILE_NAMES=0` | Disable LFN support. Saves ~5 KB flash. All our filenames are 8.3-compliant (`YYYYMMDD.CSV`, `YYYYMM/`, `TEST.TMP`). |

## Build Environments

Build environments are defined in `environments.ini`. Each environment extends a base configuration and adds target-specific flags.

### Base Configurations

| Base | Platform | Description |
|---|---|---|
| `base:esp32_main` | ESP32 | Base ESP32 build (esp32dev board) |
| `base:esp32d1_main` | ESP32 | ESP32 with RXPIN set to GPIO23 |
| `base:esp32s2_main` | ESP32-S2 | ESP32-S2 build (Lolin S2 Pico) |
| `base:esp32_oled` | ESP32 | ESP32 with SSD1306 OLED display |
| `base:esp8266_main` | ESP8266 | Base ESP8266 build (NodeMCU v2) |
| `base:esp8266_oled` | ESP8266 | ESP8266 with SSD1306 OLED display |
| `base:esp8266_neopixel` | ESP8266 | ESP8266 with NeoPixel |
| `base:espgeigerlite` | ESP8266 | ESPGeiger Lite/Log base with OLED, NeoPixel, PushButton |
| `base:espgeigerhw` | ESP8266 | ESPGeiger-HW base with OLED, PushButton, HV control |

These bases handle the platform setup, library dependencies and core feature flags. You mix and match by choosing a base that has the hardware features you need, then adding your counter type and any additional flags.

### Production Environments

| Environment | Platform | Counter Type | Features |
|---|---|---|---|
| `esp32_pulse` | ESP32 | Pulse (PCNT) | |
| `esp32_pulse_no_pcnt` | ESP32 | Pulse (interrupt) | PCNT disabled |
| `esp32oled_pulse` | ESP32 | Pulse (PCNT) | OLED |
| `esp32oled_pulse_no_pcnt` | ESP32 | Pulse (interrupt) | OLED, PCNT disabled |
| `esp32_cajoe_iotgm` | ESP32 | Pulse (PCNT) | OLED, RXPIN 26, for Cajoe IoT GM |
| `esp32_cajoe_iotgm_no_pcnt` | ESP32 | Pulse (interrupt) | OLED, RXPIN 26, PCNT disabled |
| `esp8266_pulse` | ESP8266 | Pulse | |
| `esp8266_pulsemin` | ESP8266 | Pulse | Minimal build, no cloud services |
| `esp8266oled_pulse` | ESP8266 | Pulse | OLED |
| `esp8266_serial` | ESP8266 | Serial | Runtime-selectable serial type |
| `esp8266oled_serial` | ESP8266 | Serial | OLED |
| `esp32_serial` | ESP32 | Serial | Runtime-selectable serial type |
| `esp32oled_serial` | ESP32 | Serial | OLED |
| `espgeigerhw` | ESP8266 | Pulse | OLED, PushButton, HV control |
| `espgeigerlite` | ESP8266 | Pulse | OLED, NeoPixel, PushButton |
| `espgeigerlog` | ESP8266 | Pulse | OLED, NeoPixel, PushButton, SDCard |
| `espgeigerlog_serial` | ESP8266 | Serial | OLED, NeoPixel, PushButton, SDCard |

### Test Environments

| Environment | Platform | Test Type | Features |
|---|---|---|---|
| `esp8266_test` | ESP8266 | Internal counter | |
| `esp8266oled_test` | ESP8266 | Internal counter | OLED |
| `esp32_test` | ESP32 | Internal counter | |
| `esp32oled_test` | ESP32 | Internal counter | OLED |
| `esp8266_testpulse` | ESP8266 | Pulse output | |
| `esp8266oled_testpulse` | ESP8266 | Pulse output | OLED |
| `esp32_testpulse` | ESP32 | Pulse output | |
| `esp32oled_testpulse` | ESP32 | Pulse output | OLED |
| `esp8266_testpulseint` | ESP8266 | PWM pulse output | |
| `esp8266oled_testpulseint` | ESP8266 | PWM pulse output | OLED |
| `esp32_testpulseint` | ESP32 | PWM pulse output | |
| `esp8266_testserial` | ESP8266 | Serial emulated | Type selectable at runtime |
| `esp32_testserial` | ESP32 | Serial emulated | Type selectable at runtime |
| `espgeigerlite_test` | ESP8266 | Internal counter | OLED, NeoPixel, PushButton |
| `espgeigerlite_testpulse` | ESP8266 | Pulse output | OLED, NeoPixel, PushButton |
| `espgeigerlog_test` | ESP8266 | Internal counter | OLED, NeoPixel, PushButton, SDCard |
| `espgeigerlog_testpulse` | ESP8266 | Pulse output | OLED, NeoPixel, PushButton, SDCard |
| `espgeigerlog_testpulseint` | ESP8266 | PWM pulse output | OLED, NeoPixel, PushButton, SDCard |
| `espgeigerlog_testserial` | ESP8266 | Serial emulated | OLED, NeoPixel, PushButton, SDCard |

## Making Custom Environments

Most common configurations are already covered by the pre-built environments listed above. These are built automatically by GitHub on each release and are available via the [Web Installer](https://install.espgeiger.com). Custom environments are only needed if you have non-standard hardware or want to enable a specific combination of features not covered by an existing build.

PlatformIO is configured to automatically load any file matching `*_env.ini` in the project root. To create a custom environment, add a new file such as `custom_env.ini`. This file will not be tracked by git, keeping your custom configuration separate from the project.

### Example: ESP8266 with serial counter, NeoPixel and OLED

Say you've built your own hardware with an ESP8266, a serial Geiger counter, a NeoPixel LED and an SSD1306 OLED display. You want the serial input on GPIO5 and a push button on GPIO14.

Start by choosing the right base. `base:esp8266_neopixel` gives you ESP8266 with NeoPixel support. Then add the OLED, counter type, and pin configuration on top:

```ini
[env:my_serial_neopixel_oled]
extends = base:esp8266_neopixel
lib_deps =
  ${base:esp8266_neopixel.lib_deps}
  ${libraries.ssd1306display}
build_flags =
  ${base:esp8266_neopixel.build_flags}
  -D SSD1306_DISPLAY
  -D OLED_SDA=4
  -D OLED_SCL=5
  -D GEIGER_PUSHBUTTON
  -D PUSHBUTTON_PIN=14
  -DGEIGER_TYPE=${geiger_type.serial}
  -DGEIGER_RXPIN='5'
  -DGEIGER_MODEL='"MySerial"'
```

After flashing, select your serial counter type (GC10, GC10Next, MightyOhm, or ESPGeiger) from the **Config** page under **Input > Serial Type**.

The key points when building a custom environment:

- **Pick the closest base** that has the hardware features you need. If you need NeoPixel, start with `base:esp8266_neopixel`. If you just need OLED, use `base:esp8266_oled`. For a bare board, use `base:esp8266_main` or `base:esp32_main`.
- **Add library dependencies** for any features not in the base. For example, adding OLED to a NeoPixel base requires `${libraries.ssd1306display}` in `lib_deps`.
- **Set your counter type** with `GEIGER_TYPE`. For serial builds, the specific serial protocol is selected at runtime from the Config page.
- **Configure your pins** to match your hardware wiring.
- Common output modules (MQTT, Radmon, GMC, ThingSpeak, Webhook) are enabled globally in `platformio.ini` under `[com-esp]` and will be available in all custom environments.