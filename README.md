![Build](https://github.com/steadramon/ESPGeiger/workflows/Build/badge.svg?branch=main)
# <img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/ESPGeiger.svg" width="30px"/> ESPGeiger

ESPGeiger is a project for collecting, calculating and reporting from Geiger counters with a pulse output signal. Some serial counters are supported, for example the GC10 range.
The firmware is written to be installed onto the common esp8266 and esp32 boards.

- 😃  Easy to install and configure - contribute to monitoring!
- 📈  Built in webserver with graphing
- 🔴  Live CPM and μSv/h readings
- 🔢  Smoothed and averaged values over 1, 5 and 15 minutes
- ✅  Compatible with generic Geiger counters with Pulse output and some serial based - GC10next (GC10 currently untested)
- 🎛️  Configurable filtering and noise control
- 📟  Accurate counting via interrupt and non-blocking functions (tested up to 100000CPM). Hardware counter (ESP32 only - PCNT)
- 🌐  Upload and share statistics to services online and locally via MQTT

<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/statuspage.png" width="50%"/>
<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/settings.png" width="50%"/>

## Outputs
- MQTT
- [ThingSpeak](https://thingspeak.com/channels/2087322)
- Radmon.org
- gmcmap.com

### MQTT Output

Automatically output every minute:

    ESPGeiger-129e0c/tele/lwt Online
    ESPGeiger-129e0c/tele/status {"uptime":"2T01:45:10","board":"ESP32","model":"GC10next","free_mem":191552,"ssid":"Wifi","ip":"192.168.1.123","rssi":-24}
    ESPGeiger-129e0c/stat/CPM 26.00
    ESPGeiger-129e0c/stat/uSv 0.10
    ESPGeiger-129e0c/stat/CPM5 25.00
    ESPGeiger-129e0c/stat/CPM15 25.00
    ...
    ESPGeiger-129e0c/tele/lwt Offline

### Planned:
- ESPGeiger API
- Async Webserver
- Display support
- Homeassistant discovery

## Installation

The project should build automatically with Platformio - it can be built with the Arduino IDE but will require you to satify the requirements by installing the libraries.

The `environments.ini` file defines some pre-built environments and examples of how the build can be configured. You can pick a combination of target board (esp32/esp8266) and geiger type (pulse/serial/GC10 etc)

1.  Open the PlatformIO IDE and open the ESPGeiger project.
2.  Edit the `platformio.ini` file to change `default_envs` to match the environment you want to build. 
3.  Click on the `Build` button in the PlatformIO IDE.
4.  The PlatformIO IDE will build the project.
5.  Once the project is built, you can upload it to your microcontroller.
6.  To upload the project to your microcontroller, click on the `Upload` button in the PlatformIO IDE.
7.  The PlatformIO IDE will upload the project to your microcontroller.

## Setup and Config

1.  Once installed, connect to the built-in Wifi on the ESP to set up your access point.
2.  When you connect a window show pop up asking for Wifi Configuration, if not browse to http://192.168.4.1/
3.  Select the SSID and insert the password for the network you'd like to connect to.
4.  Once connected to the network you can browse to the ESPGeiger IP address directly or enter `http://<name of the device>.local` into your browser, for example http://ESPGeiger-83e6a4.local

## Counters

The project is compatible with Generic Pulse-based geiger counters and the GC10next serial based counters.

- [DIY GeigerKit](https://sites.google.com/site/diygeigercounter/)
- [NetIO GC10](https://www.ebay.co.uk/usr/pelorymate)
- [RHElectronics](https://www.rhelectronics.store/diy-geiger-counter-kit)
- [GeigerHV](https://www.ebay.co.uk/usr/geigerhv)
- [GGreg20](https://www.tindie.com/stores/iotdev/)
- [MightyOhm Kit](https://www.tindie.com/stores/mightyohm/)
- [DiY-GDC](https://www.ebay.com/usr/impexeris)

### Serial Compatibility 

*Currently the GC10 serial integration is untested until I can gain access to a device.*

Other Serial based should in theory be supportable with small changes to the codebase.

If you own or can offer a device below for testing and support, please get in touch!

- GC10
- MightyOhm
- GMC-320
- GGreg20

## Contributions

Contributions are welcomed, please feel free to raise a Pull Request for this. I am a new Arduino/C coder, so please feel free to suggest improvements to the code here to make it better for everyone!

## Thanks 🙏
Thanks for libraries goes to:
- https://github.com/tzapu/WiFiManager
- https://github.com/khoih-prog/AsyncHTTPRequest_Generic
- https://github.com/gmag11/ESPNtpClient
- https://github.com/MattFryer/Smoothed
- https://github.com/knolleary/pubsubclient/
- https://github.com/bblanchon/ArduinoJson

And inspiration:
- https://github.com/1technophile/OpenMQTTGateway
- https://github.com/G4lile0/tinyGS/
- https://github.com/kapraran/FreqCountESP
