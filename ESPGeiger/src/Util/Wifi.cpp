/*
  Wifi.cpp - WiFi connection state and reconnect tracking.

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
#include "Wifi.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "../Logger/Logger.h"

namespace Wifi {
  bool disabled = false;
  bool connected = false;
  char ip[16] = "";
  char ssid[33] = "";
  int16_t rssi = 0;
}

static bool was_connected = false;
static unsigned long lost_at = 0;

void Wifi::tick(unsigned long now) {
  wl_status_t wifi_status = WiFi.status();
  bool prev = Wifi::connected;
  Wifi::connected = (wifi_status == WL_CONNECTED);
  if (Wifi::connected) {
    static uint8_t rssi_cnt = 0;
    if (++rssi_cnt >= 60) { rssi_cnt = 0; Wifi::rssi = WiFi.RSSI(); }
    if (!prev) {
      strncpy(Wifi::ip, WiFi.localIP().toString().c_str(), sizeof(Wifi::ip) - 1);
      strncpy(Wifi::ssid, WiFi.SSID().c_str(), sizeof(Wifi::ssid) - 1);
      Wifi::rssi = WiFi.RSSI();
    }
  }

  if (!Wifi::disabled) {
    if (wifi_status != WL_CONNECTED) {
      if (was_connected) {
        was_connected = false;
        lost_at = now;
        Log::console(PSTR("WiFi: Connection lost"));
      }
      unsigned long down_seconds = (now - lost_at) / 1000;
      if (down_seconds > 0 && down_seconds % 30 == 0) {
        Log::console(PSTR("WiFi: Attempting reconnect (%lus)"), down_seconds);
        WiFi.reconnect();
      }
      if (down_seconds > 300) {
        Log::console(PSTR("WiFi: Down for 5 minutes, rebooting"));
        delay(100);
        ESP.restart();
      }
    } else if (!was_connected) {
      was_connected = true;
      if (lost_at > 0) {
        unsigned long down_seconds = (now - lost_at) / 1000;
        Log::console(PSTR("WiFi: Reconnected after %lus"), down_seconds);
      }
    }
  }
}
