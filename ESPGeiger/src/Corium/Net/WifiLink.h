/*
  WifiLink.h - the link half of Wifi: up or down, reconnect, backups.

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
#ifndef UTIL_WIFILINK_H
#define UTIL_WIFILINK_H

#include <Arduino.h>
#include "App/AppConfig.h"

// SSID\nPSK\n
extern const char WIFI_BACKUP_P[] PROGMEM;
#define WIFI_BACKUP_PATH FPSTR(WIFI_BACKUP_P)

// static_ip\nip\ngw\nsn\ndns\n
extern const char NET_BACKUP_P[] PROGMEM;
#define NET_BACKUP_PATH FPSTR(NET_BACKUP_P)

#ifndef WIFI_DOWN_MAX_S
#define WIFI_DOWN_MAX_S 3600
#endif
static const uint32_t WIFI_OFFLINE_MAX_MS = WIFI_DOWN_MAX_S * 1000UL;

namespace Wifi {
  bool look(unsigned long now);   // true on an up or down edge
}

#endif
