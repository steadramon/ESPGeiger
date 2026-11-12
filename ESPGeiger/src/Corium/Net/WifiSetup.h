/*
  WifiSetup.h - the core's WiFi calls for Improv and boot

  Copyright (C) 2026 @steadramon

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
#ifndef CORIUM_WIFISETUP_H
#define CORIUM_WIFISETUP_H

#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#include <esp_wifi.h>
#endif

namespace corium {
namespace Wifi {
  inline bool connectedRaw() { return WiFi.status() == WL_CONNECTED; }   // live, not Wifi::connected
  inline bool apUp() { return (WiFi.getMode() & WIFI_AP) != 0; }   // the hotspot or the portal
  // saved to flash; 8266 off again or the next disconnect() wipes them
  inline void beginSaved(const char* ssid, const char* pass) {
#ifdef ESP32
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);   // persistent() is read once, at the first mode()
#endif
    WiFi.persistent(true);
    (void)WiFi.begin(ssid, pass);   // the status judges the join; the 8266 never says if the save took
#ifdef ESP8266
    WiFi.persistent(false);
#endif
  }

  // the 8266 takes the name after the mode, the esp32 only at a mode change
  inline void staMode(const char* name) {
#ifdef ESP8266
    WiFi.mode(WIFI_STA);
    WiFi.hostname(name);
#else
    WiFi.setHostname(name);
    WiFi.mode(WIFI_STA);
#endif
  }
  inline String macStr() { return WiFi.macAddress(); }
}
}  // namespace corium

#endif
