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
  constexpr bool OFFLINE_OK = true;   // No portal, hotspot 10 min, then offline
#else
  constexpr bool OFFLINE_OK = false;
#endif
  // disabled is set from OFFLINE onwards
  enum State : uint8_t { BOOT, JOINING, PORTAL, ONLINE, LINK_LOST, OFFLINE, HOTSPOT, AP_MODE, OFF };
  State state();
  const char* stateName(State s);   // name is in flash
  void go(State to, PGM_P why);   // only called by Net, sets disabled and ap
  extern bool disabled;
  extern bool offlineForced;    // Set by the app, else the wifi loop keeps retrying
  void forceOffline();   // For this boot, no retry
  extern bool connected;
  extern bool ap;                // mode 2 or the hotspot
  extern IPAddress local_ip;     // Refreshed on connect + once a minute
  extern char ssid[WIFI_SSID_MAX + 1];
  extern int16_t rssi;
  extern bool backupGraceDone;
  void checkBackupState();  // Call once LittleFS is mounted

  constexpr uint8_t TOPIC_CHANGED = 0;   // up or down

  size_t formatIP(char* buf, size_t cap);   // cap >= 16
  size_t formatIP(char* buf, size_t cap, const IPAddress& a);

  inline bool isMulticast(const IPAddress& ip) {
    return ip[0] >= 224 && ip[0] <= 239;
  }

  bool connectOrPortal();
  bool offlineNow();   // false once joined
  bool hasSavedCreds();
  bool portalAhead();   // true when none are saved and not offline
  bool apHeld(uint32_t now);   // AP still held, idle too long drops it
  uint32_t downMaxS();   // wifi.down_max in s, 0 = never
  CORIUM_NODISCARD bool joinMode();   // Sets wifi.mode back to 0, no commit
  bool offSet(const char* mod, const char* key);   // true if this set turned wifi.mode to 1
  CORIUM_NODISCARD bool keepJoined(uint32_t restart_ms);   // false = offline or not saved
  bool saveBackupCreds();   // Call before WiFi.begin(). A failed write is logged, false = nothing to back up
  bool clearSavedCreds();   // false = the sdk kept them
  bool applyStaticConfig();   // before WiFi.begin()
  bool validateStatic(const char* ip, const char* gw, const char* sn,
                      char* err, size_t cap);
  // Held creds sit on the heap until the test or beginHeld()
  enum Hold : uint8_t { HOLD_OK, HOLD_BAD, HOLD_BUSY, HOLD_NO_MEMORY };
  CORIUM_NODISCARD Hold holdCreds(const char* ssid, const char* pass);
  constexpr uint32_t JOIN_WAIT_MS = 30000;
  // Joins with the held creds in RAM, they only go to flash once up
  CORIUM_NODISCARD bool testJoin(uint32_t ms = JOIN_WAIT_MS);
  bool testStep(uint32_t now);   // true = still testing
  uint8_t testState();   // an EGPortal::Join
  bool testing();   // Testing, or going back to the old network
  const char* testSsid();   // ssid under test, or the last one tested
  constexpr size_t LAST_TRY_MAX = 64 + 6 * WIFI_SSID_MAX;   // html with the ssid escaped
  size_t lastTry(char* buf, size_t cap);   // Only given once, 0 = none
  const char* heldSsid();   // "" if none
  bool beginHeld();   // WiFi.begin() then frees them, false = none held
  void dropHeld();   // For a save that didn't land
  CORIUM_NODISCARD bool saveNetBackup();   // true = a backup exists, a pending one is kept
  // Fields are static, ip, gw, sn, dns. No commit
  CORIUM_NODISCARD bool putNet(const char* const* f);
  bool probeReachability();   // Blocks, lwIP timeout ~14s
  bool restoreNetBackup();   // Restart after
  // Called on a reset, else a later failed save would revert to it
  void dropNetBackup();
  void applyRuntimeNetPrefs();   // after WiFi.mode()
  void rxSleep(int8_t mode);   // -1 = the wifi.sleep pref
  void applySleep();   // Re-applies it, the 8266 Update.begin turns it off
  void reconnect();

  int txPowerQdbm();
  const char* phyModeStr();
  const char* sleepModeStr();
}
}  // namespace corium

#endif
