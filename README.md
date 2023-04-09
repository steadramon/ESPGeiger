![Build](https://github.com/steadramon/ESPGeiger/workflows/Build/badge.svg?branch=main)
# <img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/ESPGeiger.svg" width="30px"/> ESPGeiger

 ESPGeiger is a project for collecting, calculating and reporting from Geiger counters with a pulse output signal. The firmware is written to be installed onto the common esp8266 and esp32 boards.

- 😃  Easy to install and configure - contribute to monitoring!
- 📈  Built in webserver with graphing
- 🔴  Live CPM and μSv/h readings
- 🔢  Smoothed and averaged values over 1, 5 and 15 minutes
- ✅  Compatible with generic Geiger counters with Pulse output and GC10next (GC10 currently untested)
- 🎛️  Configurable filtering and noise control
- 📟  Hardware counter (ESP32 only - PCNT)

<img src="https://raw.githubusercontent.com/steadramon/ESPGeiger/main/docs/img/statuspage.png"/>

## Outputs
- MQTT
- ThingSpeak
- Radmon.org
- gmcmap.com

Planned:
- ESPGeiger API
- Async Webserver
- Display support

## Geiger counters

The project is compatible with Generic Pulse-based geiger counters and the GC10next serial based counters.

- [DIY GeigerKit](https://sites.google.com/site/diygeigercounter/)
- [NetIO GC10](https://www.ebay.co.uk/usr/pelorymate)
- [RHElectronics](https://www.rhelectronics.store/diy-geiger-counter-kit)
- [GeigerHV](https://www.ebay.co.uk/usr/geigerhv)
- [GGreg20](https://www.tindie.com/stores/iotdev/)
- [MightOhm Kit](https://www.tindie.com/stores/mightyohm/)

*Currently the GC10 integration is untested until I can gain access to a device.*

Other Serial based should in theory be supportable with small changes to the codebase.

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
