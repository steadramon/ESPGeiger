/*
  Wifi.h - WiFi connection state and reconnect tracking.
  Storage as plain extern vars so readers pay zero indirection.

  Copyright (C) 2026 @steadramon
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
