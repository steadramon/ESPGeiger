/*
  Wifi.h - WiFi connection state and reconnect tracking.
  Storage as plain extern vars so readers pay zero indirection.

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
#ifndef UTIL_WIFI_H
#define UTIL_WIFI_H

#include <Arduino.h>

namespace Wifi {
  extern bool disabled;
  extern bool connected;
  extern char ip[16];
  extern char ssid[33];
  extern int16_t rssi;

  void tick(unsigned long now);
}

#endif
