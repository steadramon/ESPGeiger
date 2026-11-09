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
#ifndef CORIUM_WIFI_H
#define CORIUM_WIFI_H

#include <Arduino.h>
#include <IPAddress.h>
#include "App/AppConfig.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/Attr.h"
#include "WifiStable.h"

#define WIFI_SSID_MAX 32   // 802.11

namespace corium {
namespace Wifi {
#ifdef APP_OFFLINE_OK
  constexpr bool OFFLINE_OK = true;   // saved wifi fails: no portal, stay offline
#else
  constexpr bool OFFLINE_OK = false;
#endif
  extern bool disabled;
  extern bool offlineForced;    // app sets it, else the wifi loop retries
  inline void forceOffline() { disabled = true; offlineForced = true; }   // this boot, no retry
  extern bool connected;
  extern bool ap;                // wifi.mode 2, its own AP up
  extern IPAddress local_ip;     // refreshed on connect + once a minute
  extern char ssid[WIFI_SSID_MAX + 1];
  extern int16_t rssi;
  extern bool backupGraceDone;
  void checkBackupState();  // LittleFS mounted

  constexpr uint8_t TOPIC_CHANGED = 0;   // up or down

  size_t formatIP(char* buf, size_t cap);   // cap >= 16
  size_t formatIP(char* buf, size_t cap, const IPAddress& a);

  inline bool isMulticast(const IPAddress& ip) {
    return ip[0] >= 224 && ip[0] <= 239;
  }

  bool connectOrPortal();
  bool offlineNow();   // console: this boot without wifi, false once joined
  bool hasSavedCreds();
  bool portalAhead();   // first setup: none saved, not offline
  void joinMode();   // wifi.mode back to 0, no commit
  bool saveBackupCreds();   // before WiFi.begin()
  bool clearSavedCreds();   // false = the sdk kept them, logged
  bool applyStaticConfig();   // before WiFi.begin()
  bool validateStatic(const char* ip, const char* gw, const char* sn,
                      char* err, size_t cap);
  // heap from save to the test or beginHeld(); bad = no pass or too long, busy = one held already
  enum Hold : uint8_t { HOLD_OK, HOLD_BAD, HOLD_BUSY, HOLD_NO_MEMORY };
  CORIUM_NODISCARD Hold holdCreds(const char* ssid, const char* pass);
  constexpr uint32_t JOIN_WAIT_MS = 30000;
  // the held creds joined in RAM, on flash only once up; false = none held, one running or no memory
  CORIUM_NODISCARD bool testJoin(uint32_t ms = JOIN_WAIT_MS);
  void testStep(uint32_t now);   // the portal loop or the wifi module, while testing()
  uint8_t testState();   // an EGPortal::Join, the last one kept till the next test
  bool testing();   // testing or going back
  const char* testSsid();   // under test or the last one
  const char* heldSsid();   // "" if none
  bool beginHeld();   // WiFi.begin() then frees, false = none held
  void dropHeld();   // a save that didn't land
  bool saveNetBackup();   // won't overwrite a pending backup
  bool probeReachability();   // blocks, lwIP timeout ~14s
  bool restoreNetBackup();   // restart after
  // on reset, else a later failed save reverts
  void dropNetBackup();
  void applyRuntimeNetPrefs();   // after WiFi.mode()
  void rxSleep(int8_t mode);   // a receiver's sleep mode, -1 = the wifi.sleep pref
  void reconnect();

  int txPowerQdbm();
  const char* phyModeStr();
  const char* sleepModeStr();
}
}  // namespace corium

#endif
