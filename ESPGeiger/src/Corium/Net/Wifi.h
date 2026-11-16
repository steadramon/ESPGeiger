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
  constexpr bool OFFLINE_OK = true;   // saved wifi fails: no portal, a hotspot 10 min, then offline
#else
  constexpr bool OFFLINE_OK = false;
#endif
  // boot = before the join; disabled from OFFLINE on
  enum State : uint8_t { BOOT, JOINING, PORTAL, ONLINE, LINK_LOST, OFFLINE, HOTSPOT, AP_MODE, OFF };
  State state();
  const char* stateName(State s);   // flash
  void go(State to, PGM_P why);   // Net only, sets disabled and ap
  extern bool disabled;
  extern bool offlineForced;    // app sets it, else the wifi loop retries
  void forceOffline();   // this boot, no retry
  extern bool connected;
  extern bool ap;                // wifi.mode 2 or the hotspot, its own AP up
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
  bool apHeld(uint32_t now);   // the fallback AP still up; idle past its time it goes, false
  CORIUM_NODISCARD bool joinMode();   // wifi.mode back to 0, no commit
  CORIUM_NODISCARD bool keepJoined(uint32_t restart_ms);   // a test joined: true = it stays; false = offline this boot or not saved
  bool saveBackupCreds();   // before WiFi.begin(); a failed write logs, none to back up = false
  bool clearSavedCreds();   // false = the sdk kept them, logged
  bool applyStaticConfig();   // before WiFi.begin()
  bool validateStatic(const char* ip, const char* gw, const char* sn,
                      char* err, size_t cap);
  // heap from save to the test or beginHeld(); bad = no pass or too long, busy = one held already
  enum Hold : uint8_t { HOLD_OK, HOLD_BAD, HOLD_BUSY, HOLD_NO_MEMORY };
  CORIUM_NODISCARD Hold holdCreds(const char* ssid, const char* pass);
  constexpr uint32_t JOIN_WAIT_MS = 30000;
  // the held creds joined in RAM, on flash only once up; right after HOLD_OK. false = none held or no memory
  CORIUM_NODISCARD bool testJoin(uint32_t ms = JOIN_WAIT_MS);
  bool testStep(uint32_t now);   // the portal loop or the wifi module, while testing(); true = still testing
  uint8_t testState();   // an EGPortal::Join, the last one kept till the next test
  bool testing();   // testing or going back
  const char* testSsid();   // under test or the last one
  constexpr size_t LAST_TRY_MAX = 64 + 6 * WIFI_SSID_MAX;   // html, the ssid escaped
  size_t lastTry(char* buf, size_t cap);   // a failed join's line for the pages, once; 0 = none
  const char* heldSsid();   // "" if none
  bool beginHeld();   // WiFi.begin() then frees, false = none held
  void dropHeld();   // a save that didn't land
  CORIUM_NODISCARD bool saveNetBackup();   // true = one there, a pending one kept
  // static, ip, gw, sn, dns; no commit. false = one refused, the rest put
  CORIUM_NODISCARD bool putNet(const char* const* f);
  bool probeReachability();   // blocks, lwIP timeout ~14s
  bool restoreNetBackup();   // restart after
  // on reset, else a later failed save reverts
  void dropNetBackup();
  void applyRuntimeNetPrefs();   // after WiFi.mode()
  void rxSleep(int8_t mode);   // a receiver's sleep mode, -1 = the wifi.sleep pref
  void applySleep();   // again, the 8266 Update.begin turns it off
  void reconnect();

  int txPowerQdbm();
  const char* phyModeStr();
  const char* sleepModeStr();
}
}  // namespace corium

#endif
