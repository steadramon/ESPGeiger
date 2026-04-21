/*
  DeviceInfo.h - Device identity and utility functions.
  Computed once at boot by ConfigManager, readable from anywhere
  without pulling in the WiFiManager dependency chain.

  Copyright (C) 2025 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/
#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <Arduino.h>
#include <ESPNtpClient.h>

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "devel"
#endif

#ifndef BUILD_ENV
#define BUILD_ENV "unknown"
#endif

#ifndef THING_NAME
#define THING_NAME "ESPGeiger"
#endif

namespace DeviceInfo {
  void init(const char* hostName, const char* chipId,
            const char* userAgent, const char* macAddr);

  const char* hostname();
  const char* chipid();
  const char* useragent();
  const char* chipmodel();
  const char* geigermodel();
  const char* mac();

  inline unsigned long uptime() { return NTP.getUptime(); }

  char* uptimeString();

  // Normalised reset-reason code. ESP8266 rst_info.reason and ESP32
  // esp_reset_reason() use different enums; this maps both to a single
  // small set so the census can aggregate cross-platform. Codes are
  // frozen — never renumber.
  //   0 unknown   1 power-on     2 external reset   3 software restart
  //   4 exception 5 watchdog     6 brown-out        7 deep-sleep wake
  uint8_t resetReason();

  // Compile-time feature bitmask. Bits track *optional* modules that may or
  // may not be built in. Board variants live in chipmodel/`btd`; input types
  // in BUILD_ENV/`gm`; WebAPI itself is a given (we're inside its handshake).
  // Always-on modules (NTP, Counter, ConfigManager, etc.) aren't tracked —
  // a bit that's always 1 carries no information. Bits are frozen — never
  // reassign.
  //   bit 0  MQTT          (MQTTOUT)
  //   bit 1  Radmon        (RADMONOUT)
  //   bit 2  ThingSpeak    (THINGSPEAKOUT)
  //   bit 3  Webhook       (WEBHOOKOUT)
  //   bit 4  Serial out    (SERIALOUT)
  //   bit 5  GMC out       (GMCOUT)
  //   bit 6  OLED display  (SSD1306_DISPLAY)
  //   bit 7  Push button   (GEIGER_PUSHBUTTON)
  //   bit 8  SD logger     (GEIGER_SDCARD)
  //   bit 9  NeoPixel      (GEIGER_NEOPIXEL)
  uint16_t featureFlags();
}

#endif
