/*
  WifiInfo.h - live reads of the core's WiFi, apart from Wifi.h
  so host tests keep their own WiFi fakes.

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
#ifndef CORIUM_WIFIINFO_H
#define CORIUM_WIFIINFO_H

#include <IPAddress.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

namespace corium {
// live, Wifi::local_ip and Wifi::ssid are the cached ones
namespace Wifi {
  inline IPAddress localIP()    { return WiFi.localIP(); }
  inline IPAddress gatewayIP()  { return WiFi.gatewayIP(); }
  inline IPAddress subnetMask() { return WiFi.subnetMask(); }
  inline IPAddress dnsIP()      { return WiFi.dnsIP(); }
  inline IPAddress softAPIP()   { return WiFi.softAPIP(); }
  inline void macAddress(uint8_t* mac)       { WiFi.macAddress(mac); }   // 6 bytes
  inline void softAPmacAddress(uint8_t* mac) { WiFi.softAPmacAddress(mac); }
  inline const uint8_t* BSSID() { return WiFi.BSSID(); }   // nullptr = none
  inline String SSID()          { return WiFi.SSID(); }
  inline String softAPSSID()    { return WiFi.softAPSSID(); }
  inline bool isConnected()     { return WiFi.isConnected(); }
  inline int32_t channel()      { return WiFi.channel(); }
  inline bool getAutoReconnect() { return WiFi.getAutoReconnect(); }

  inline int scanComplete()    { return WiFi.scanComplete(); }
  inline void scanDelete()     { WiFi.scanDelete(); }
  inline int scanNetworks(bool async, bool hidden) { return WiFi.scanNetworks(async, hidden); }
}
}  // namespace corium

#endif
