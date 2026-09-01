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
#include <IPAddress.h>
#include "../System/FastMillis.h"
#include "WifiStable.h"

#define WIFI_SSID_MAX 32   // 802.11

namespace Wifi {
  extern bool disabled;
  extern bool offline_forced;    // button-hold only, else tick retries
  extern bool connected;
  extern IPAddress local_ip;     // refreshed on connect + once a minute
  extern char ssid[WIFI_SSID_MAX + 1];
  extern int16_t rssi;
  extern bool backup_grace_done;
  void check_backup_state();  // LittleFS mounted

  void tick(unsigned long now);

  size_t formatIP(char* buf, size_t cap);   // cap >= 16
  size_t formatIP(char* buf, size_t cap, const IPAddress& a);

  inline bool is_multicast(const IPAddress& ip) {
    return ip[0] >= 224 && ip[0] <= 239;
  }

  bool connectOrPortal();
  bool hasSavedCreds();
  bool saveBackupCreds();   // before WiFi.begin()
  void clearSavedCreds();
  bool applyStaticConfig();   // before WiFi.begin()
  bool validateStatic(const char* s_ip, const char* s_gw, const char* s_sn,
                      char* errbuf, size_t errlen);
  void onImprovCreds(const char* ssid, const char* pass);
  // heap from save to beginHeld(), false = too long or no memory
  bool holdCreds(const char* ssid, const char* pass);
  const char* heldSsid();   // "" if none
  bool beginHeld();   // WiFi.begin() then frees, false = none held
  bool saveNetBackup();   // won't overwrite a pending backup
  bool probeReachability();   // blocks, lwIP timeout ~14s
  bool restoreNetBackup();   // restart after
  // on reset, else a later failed save reverts
  void dropNetBackup();
  void applyRuntimeNetPrefs();   // after WiFi.mode()
  void rx_sleep(int8_t mode);   // UdpRx's sleep mode, -1 = the wifi.sleep pref

  int txPowerQdbm();
  const char* phyModeStr();
  const char* sleepModeStr();
}

#endif
