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
#include <esp_wifi.h>   // esp_wifi_get_config() for direct NVS cred lookup
#endif
#include <LittleFS.h>
#include <EGPortal.h>
#include "../Logger/Logger.h"
#include "../Prefs/EGPrefs.h"
#include "DeviceInfo.h"

// Two-line file: SSID\nPSK\n. Owned by /wifisave: writes before applying
// new creds, deleted on successful revert OR after 5 min stable boot.
static const char WIFI_BACKUP_PATH[] = "/wifi_backup";

// Five-line file: static_ip\nip\ngw\nsn\ndns\n. Saved by /wifisave when
// the user changes net.* prefs; restored on probe failure or after 5 min
// stable boot, whichever comes first.
static const char NET_BACKUP_PATH[] = "/net_backup";

// Setup-portal handoff state - captured by EGPortal::onSave callback,
// consumed once the user finishes the captive portal flow.
static char     s_portalSsid[33];
static char     s_portalPass[65];
static volatile bool s_portalGotCreds = false;

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

  // Backup-revert grace: 5 minutes after a stable boot with the new
  // creds, drop /wifi_backup AND /net_backup. The user has clearly
  // succeeded - both rollback safety nets can retire.
  static bool backup_grace_done = false;
  if (!backup_grace_done && Wifi::connected && now > 300000UL) {
    backup_grace_done = true;
    if (LittleFS.begin()) {
      if (LittleFS.exists(WIFI_BACKUP_PATH)) {
        LittleFS.remove(WIFI_BACKUP_PATH);
        Log::console(PSTR("WiFi: New creds stable for 5 min, backup cleared"));
      }
      if (LittleFS.exists(NET_BACKUP_PATH)) {
        LittleFS.remove(NET_BACKUP_PATH);
        Log::console(PSTR("WiFi: Static IP stable for 5 min, net backup cleared"));
      }
      LittleFS.end();
    }
  }
}

// ---------- boot-time association ----------

static bool waitForWifi(uint32_t timeoutMs) {
  uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Log::console(PSTR("WiFi: IP: %s"), WiFi.localIP().toString().c_str());
      return true;
    }
    delay(100);
    yield();
  }
  return false;
}

bool Wifi::hasSavedCreds() {
  WiFi.mode(WIFI_STA);
#ifdef ESP32
  // esp_wifi_get_config() can return stale empty data right after the
  // mode change - the SDK reads persistent NVS creds asynchronously.
  // Read the SDK config directly so we don't depend on WiFi.SSID()'s
  // timing. Equivalent to WiFiManager::getWiFiIsSaved() (which the
  // legacy ConfigManager wrapped) but without dragging WiFiManager in.
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK &&
      conf.sta.ssid[0] != '\0') {
    return true;
  }
  // Fallback: brief delay and retry via the high-level API. Belt-and-
  // braces for SDK timing edge cases.
  delay(50);
  return WiFi.SSID().length() > 0;
#else
  return WiFi.SSID().length() > 0;
#endif
}

void Wifi::clearSavedCreds() {
  // Wipe SDK-stored STA credentials. Both ESP8266 and ESP32 expose a
  // disconnect() overload that (with the right flags) zeroes the
  // persisted config. Replaces WiFiManager::resetSettings().
#ifdef ESP8266
  WiFi.persistent(true);
  WiFi.disconnect(true);          // wifioff=true also clears stored creds
#else
  WiFi.persistent(true);
  WiFi.disconnect(true, true);    // disable + eraseAP
#endif
  Log::console(PSTR("WiFi: SDK creds cleared"));
}

bool Wifi::saveBackupCreds() {
  String ssid = WiFi.SSID();
  if (ssid.length() == 0) return false;     // nothing to back up
  if (!LittleFS.begin()) return false;
  File f = LittleFS.open(WIFI_BACKUP_PATH, "w");
  if (!f) { LittleFS.end(); return false; }
  f.print(ssid);
  f.print('\n');
  f.print(WiFi.psk());
  f.print('\n');
  f.close();
  LittleFS.end();
  Log::console(PSTR("WiFi: Backup saved (ssid=%s)"), ssid.c_str());
  return true;
}

// Read up to `nFields` newline-delimited records from `f` into `buf`,
// null-terminating each in place and trimming trailing CR/space.
// Returns the count actually populated. fields[i] points into `buf`
// and stays valid until the next read into `buf`. Avoids the heap
// allocs that String/readStringUntil require for each line.
static uint8_t readLines(File& f, char* buf, size_t bufLen,
                         char** fields, uint8_t nFields) {
  if (bufLen == 0 || nFields == 0) return 0;
  int n = f.read((uint8_t*)buf, bufLen - 1);
  if (n < 0) n = 0;
  buf[n] = '\0';
  uint8_t idx = 0;
  fields[idx++] = buf;
  for (char* p = buf; *p && idx < nFields; p++) {
    if (*p == '\n') { *p = '\0'; fields[idx++] = p + 1; }
  }
  // Pad remaining slots with empty string + trim each populated field.
  for (uint8_t i = 0; i < nFields; i++) {
    if (i >= idx) { fields[i] = (char*)""; continue; }
    char* s = fields[i];
    size_t l = strlen(s);
    while (l > 0) {
      char c = s[l - 1];
      if (c == '\r' || c == ' ' || c == '\t') s[--l] = '\0';
      else break;
    }
  }
  return idx;
}

// Try the /wifi_backup creds. Returns true if association succeeded; the
// backup file is removed on success so the next reboot uses the now-
// active config from SDK NVS.
static bool tryRestoreBackup() {
  if (!LittleFS.begin()) return false;
  if (!LittleFS.exists(WIFI_BACKUP_PATH)) { LittleFS.end(); return false; }
  File f = LittleFS.open(WIFI_BACKUP_PATH, "r");
  if (!f) { LittleFS.end(); return false; }
  // 33 + 1 + 64 + 1 = 99; round up to 128 for headroom.
  char raw[128];
  char* fields[2];
  readLines(f, raw, sizeof(raw), fields, 2);
  f.close();
  const char* ssid = fields[0];
  const char* pass = fields[1];
  if (!*ssid) {
    LittleFS.remove(WIFI_BACKUP_PATH);
    LittleFS.end();
    return false;
  }
  Log::console(PSTR("WiFi: Restoring backup creds (ssid=%s)"), ssid);
  WiFi.persistent(true);
  WiFi.begin(ssid, pass);
  bool ok = waitForWifi(30000);
  if (ok) {
    LittleFS.remove(WIFI_BACKUP_PATH);
    Log::console(PSTR("WiFi: Reverted to backup creds"));
  }
  LittleFS.end();
  return ok;
}

bool Wifi::saveNetBackup() {
  if (!LittleFS.begin()) return false;
  // Refuse to clobber a pending backup - that would lose the
  // last-known-good state if probe was already arming for an earlier
  // change.
  if (LittleFS.exists(NET_BACKUP_PATH)) {
    LittleFS.end();
    return false;
  }
  File f = LittleFS.open(NET_BACKUP_PATH, "w");
  if (!f) { LittleFS.end(); return false; }
  f.printf("%s\n%s\n%s\n%s\n%s\n",
           EGPrefs::getBool("net", "static_ip") ? "1" : "0",
           EGPrefs::getString("net", "ip"),
           EGPrefs::getString("net", "gw"),
           EGPrefs::getString("net", "sn"),
           EGPrefs::getString("net", "dns"));
  f.close();
  LittleFS.end();
  return true;
}

bool Wifi::probeReachability() {
  IPAddress dummy;
  return WiFi.hostByName("pool.ntp.org", dummy);
}

bool Wifi::restoreNetBackup() {
  if (!LittleFS.begin()) return false;
  if (!LittleFS.exists(NET_BACKUP_PATH)) { LittleFS.end(); return false; }
  File f = LittleFS.open(NET_BACKUP_PATH, "r");
  if (!f) { LittleFS.end(); return false; }
  // 5 fields × max 16 (IPv4 string) + newlines = ~85 B; 96 has headroom.
  char raw[96];
  char* fields[5];
  readLines(f, raw, sizeof(raw), fields, 5);
  f.close();
  EGPrefs::put("net", "static_ip", fields[0]);
  EGPrefs::put("net", "ip",        fields[1]);
  EGPrefs::put("net", "gw",        fields[2]);
  EGPrefs::put("net", "sn",        fields[3]);
  EGPrefs::put("net", "dns",       fields[4]);
  EGPrefs::commit();
  LittleFS.remove(NET_BACKUP_PATH);
  LittleFS.end();
  return true;
}

bool Wifi::validateStatic(const char* s_ip, const char* s_gw, const char* s_sn,
                          char* errbuf, size_t errlen) {
  IPAddress ip, gw, sn;
  if (!ip.fromString(s_ip)) {
    if (errbuf) snprintf_P(errbuf, errlen, PSTR("invalid IP address: %s"), s_ip);
    return false;
  }
  if (!gw.fromString(s_gw)) {
    if (errbuf) snprintf_P(errbuf, errlen, PSTR("invalid gateway: %s"), s_gw);
    return false;
  }
  if (!sn.fromString(s_sn)) {
    if (errbuf) snprintf_P(errbuf, errlen, PSTR("invalid subnet mask: %s"), s_sn);
    return false;
  }
  // Subnet-sanity: gateway must be reachable from the configured IP via
  // direct ARP, i.e. (ip & sn) == (gw & sn). Catches the common typo
  // class (e.g. ip=192.168.1.x, sn=255.255.255.0, gw=192.168.2.1) that
  // the DNS-on-local-subnet probe can miss.
  if (((uint32_t)ip & (uint32_t)sn) != ((uint32_t)gw & (uint32_t)sn)) {
    if (errbuf) snprintf_P(errbuf, errlen,
      PSTR("gateway %s outside subnet %s/%s"), s_gw, s_ip, s_sn);
    return false;
  }
  return true;
}

bool Wifi::applyStaticConfig() {
  if (!EGPrefs::getBool("net", "static_ip")) return false;
  const char* s_ip  = EGPrefs::getString("net", "ip");
  const char* s_gw  = EGPrefs::getString("net", "gw");
  const char* s_sn  = EGPrefs::getString("net", "sn");
  const char* s_dns = EGPrefs::getString("net", "dns");
  char err[96];
  if (!Wifi::validateStatic(s_ip, s_gw, s_sn, err, sizeof(err))) {
    Log::console(PSTR("WiFi: Static IP rejected (%s); using DHCP"), err);
    return false;
  }
  IPAddress ip, gw, sn, dns;
  ip.fromString(s_ip);
  gw.fromString(s_gw);
  sn.fromString(s_sn);
  // DNS is optional - fall back to gateway if blank/invalid.
  if (!dns.fromString(s_dns)) dns = gw;
  WiFi.config(ip, gw, sn, dns);
  Log::console(PSTR("WiFi: Static %s gw=%s sn=%s dns=%s"),
               ip.toString().c_str(), gw.toString().c_str(),
               sn.toString().c_str(), dns.toString().c_str());
  return true;
}

bool Wifi::connectOrPortal() {
  // Try saved creds first.
  if (Wifi::hasSavedCreds()) {
    WiFi.mode(WIFI_STA);
    bool staticOn = Wifi::applyStaticConfig();
    WiFi.begin();                            // uses NVS-stored creds
    if (waitForWifi(30000)) {
      // Static IP probe-and-revert: associated radio-side, but if the
      // user typed a wrong gateway/subnet the device is unreachable on
      // the LAN. /net_backup is left by /wifisave; if reachability fails,
      // restore the previous net.* values and reboot back to a known-good
      // config. Five-min stable grace clears the backup in tick().
      bool hasBackup = false;
      if (LittleFS.begin()) {
        hasBackup = LittleFS.exists(NET_BACKUP_PATH);
        LittleFS.end();
      }
      if (staticOn && hasBackup) {
        Log::console(PSTR("WiFi: Probing reachability of new static config"));
        if (Wifi::probeReachability()) {
          Log::console(PSTR("WiFi: Static config probe OK"));
        } else {
          Log::console(PSTR("WiFi: Static config probe failed, reverting"));
          Wifi::restoreNetBackup();
          delay(500);
          ESP.restart();
        }
      }
      return true;
    }
    Log::console(PSTR("WiFi: Saved creds failed to associate"));
    // If a recent /wifisave left a backup, give it a chance before
    // dropping into captive setup. Static config (if any) carries over -
    // not cleared here so the backup network gets the same addressing.
    if (tryRestoreBackup()) return true;
  }

  // Fresh setup: bring up captive AP and wait for the user to pick a
  // network. Heap-allocated; sized for the scan-cache + DNS + HTTP
  // server inside (~1.5 KB).
  s_portalGotCreds = false;
  auto* portal = new EGPortal();
  portal->setTitle(THING_NAME);
  // Notice rendered above the form. Lives on s_portalNotice for the
  // portal's lifetime - the EGPortal stores the pointer, doesn't copy.
  static char s_portalNotice[160];
  snprintf_P(s_portalNotice, sizeof(s_portalNotice),
    PSTR("<p style='margin:0 0 1em;background:#eef;padding:.6em .8em;"
         "border-radius:.3em;font-size:.85em;line-height:1.4'>"
         "After saving, find this device at <b>%s.local</b> on your network.</p>"),
    DeviceInfo::hostname());
  portal->setNotice(s_portalNotice);
  portal->onSave([](const char* ssid, const char* pw, void*) {
    strncpy(s_portalSsid, ssid, sizeof(s_portalSsid) - 1);
    s_portalSsid[sizeof(s_portalSsid) - 1] = '\0';
    strncpy(s_portalPass, pw, sizeof(s_portalPass) - 1);
    s_portalPass[sizeof(s_portalPass) - 1] = '\0';
    s_portalGotCreds = true;
  });
  portal->begin(DeviceInfo::hostname());
  Log::console(PSTR("WiFi: Setup portal up on AP %s"), DeviceInfo::hostname());
  while (!s_portalGotCreds) {
    portal->loop();
    delay(10);
    yield();
  }
  portal->end();
  delete portal;
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  Wifi::applyStaticConfig();
  WiFi.begin(s_portalSsid, s_portalPass);
  return waitForWifi(30000);
}
