/*
  WifiSetup.h - the core's WiFi calls for Improv and boot, apart from
  WifiInfo.h so the portal's host fakes stay small.

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
#ifndef UTIL_WIFISETUP_H
#define UTIL_WIFISETUP_H

#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

namespace Wifi {
  inline bool connected_raw() { return WiFi.status() == WL_CONNECTED; }   // live, not Wifi::connected
  // saved to flash; 8266 off again or the next disconnect() wipes them
  inline void begin_saved(const char* ssid, const char* pass) {
    WiFi.persistent(true);
    WiFi.begin(ssid, pass);
#ifdef ESP8266
    WiFi.persistent(false);
#endif
  }
  inline void join(const char* ssid, const char* pass) {
    WiFi.disconnect(false);
    begin_saved(ssid, pass);
  }
  inline void off() { WiFi.disconnect(true); }

  // scan results, after Wifi::scanNetworks()
  inline String scanSSID(int i)  { return WiFi.SSID(i); }
  inline int32_t scanRSSI(int i) { return WiFi.RSSI(i); }
  inline bool scanOpen(int i) {
#ifdef ESP8266
    return WiFi.encryptionType(i) == ENC_TYPE_NONE;
#else
    return WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
#endif
  }

  inline void staMode() { WiFi.mode(WIFI_STA); }
  inline void setHostname(const char* name) {
#ifdef ESP8266
    WiFi.hostname(name);
#else
    WiFi.setHostname(name);
#endif
  }
  inline String macStr() { return WiFi.macAddress(); }
}

#endif
