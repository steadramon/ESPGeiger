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
#include "FastMillis.h"

namespace Wifi {
  extern bool disabled;
  extern bool connected;
  extern IPAddress local_ip;     // refreshed on connect + once a minute
  extern char ssid[33];
  extern int16_t rssi;
  extern unsigned long connected_at_ms;  // millis() of last clean STA association; 0 when down
  extern bool backup_grace_done;
  void check_backup_state();  // call once at boot with LittleFS already mounted

  void tick(unsigned long now);

  // True when wifi has been connected at least `ms`. EGModuleRegistry
  // gates FLAG_REQUIRES_WIFI modules on this so AsyncTCP can drain stale
  // callbacks after reconnect before we hand it new work.
  inline bool stable_for(unsigned long ms) {
    return connected_at_ms != 0 && (fast_millis() - connected_at_ms) >= ms;
  }

  // Format local_ip as dotted-quad into buf. Needs cap >= 16. Returns
  // chars written (excludes NUL). Use when a string form is needed for
  // display or text protocols.
  size_t formatIP(char* buf, size_t cap);

  // IPv4 multicast = 224.0.0.0/4.
  inline bool is_multicast(const IPAddress& ip) {
    return ip[0] >= 224 && ip[0] <= 239;
  }

  // Boot-time WiFi association: try saved creds first, fall back to the
  // EGPortal captive setup AP. Implements a backup/revert protocol - if
  // a /wifi_backup file exists from a recent /wifisave attempt and the
  // current creds fail, the backup is restored before dropping into
  // setup mode. Returns true on success.
  bool connectOrPortal();

  // Returns true if the SDK has STA credentials stored. Used to skip the
  // 30 s connect timeout when there's nothing to try yet.
  bool hasSavedCreds();

  // Persist current STA SSID/PSK to /wifi_backup so a failed /wifisave
  // can revert. Call BEFORE WiFi.begin(new_ssid, new_pass).
  bool saveBackupCreds();

  // Erase the SDK-stored STA credentials. Replaces WiFiManager::
  // resetSettings() so the WiFiManager dependency can be dropped.
  void clearSavedCreds();

  // Read net.* prefs and call WiFi.config() with static IP/gw/sn/dns when
  // net.static_ip is set. No-op when DHCP is selected. Must be called
  // BEFORE WiFi.begin(); ESP8266/ESP32 latch the addressing mode at the
  // point begin() runs. Returns true if static config was applied.
  bool applyStaticConfig();

  // Validate string-form static IP config (parses + subnet-sanity).
  // Returns true if the values would yield a usable LAN config. On
  // failure, writes a human-readable reason into errbuf if provided.
  // Used by /wifisave for immediate form-time feedback, and by
  // applyStaticConfig at boot - single source of truth so the two paths
  // can't drift.
  bool validateStatic(const char* s_ip, const char* s_gw, const char* s_sn,
                      char* errbuf, size_t errlen);

  // Snapshot current net.* prefs to /net_backup for the auto-revert flow.
  // Refuses to overwrite an existing backup (a previous network change is
  // still pending verification - clobbering it would lose the
  // last-known-good state). Call before EGPrefs::put for net.* keys.
  bool saveNetBackup();

  // Reachability probe - `WiFi.hostByName` against pool.ntp.org. lwIP
  // default timeout (~14 s). Routes through the gateway and DNS, so it
  // catches both bad gateway and bad DNS configs in one shot.
  bool probeReachability();

  // Read /net_backup and write the values back into net.* prefs +
  // commit. Caller is expected to ESP.restart() afterwards.
  bool restoreNetBackup();
}

#endif
