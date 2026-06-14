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
#include <user_interface.h>
#else
#include <WiFi.h>
#include <esp_wifi.h>   // esp_wifi_get_config() for direct NVS cred lookup
#endif
#include <LittleFS.h>
#include <EGPortal.h>
#include "../Logger/Logger.h"
#include "../Module/EGModule.h"
#include "../ImprovSerial/ImprovSerial.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "DeviceInfo.h"

// SSID\nPSK\n - cleared on revert or 5-min stable boot.
static const char WIFI_BACKUP_PATH[] = "/wifi_backup";

// static_ip\nip\ngw\nsn\ndns\n - restored on probe fail or 5-min stable.
static const char NET_BACKUP_PATH[] = "/net_backup";

// Captive-portal handoff, captured by EGPortal::onSave.
static char     s_portalSsid[33];
static char     s_portalPass[65];
static volatile bool s_portalGotCreds = false;

namespace Wifi {
  bool disabled = false;
  bool connected = false;
  IPAddress local_ip;
  char ssid[33] = "";
  int16_t rssi = 0;
  unsigned long connected_at_ms = 0;
  bool backup_grace_done = false;

  void check_backup_state() {
    backup_grace_done = !LittleFS.exists(WIFI_BACKUP_PATH)
                     && !LittleFS.exists(NET_BACKUP_PATH);
  }

  size_t formatIP(char* buf, size_t cap) {
    return snprintf(buf, cap, "%u.%u.%u.%u",
                    local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
  }
}

static bool was_connected = false;
static unsigned long lost_at = 0;

void Wifi::tick(unsigned long now) {
  wl_status_t wifi_status = WiFi.status();
  bool prev = Wifi::connected;
  Wifi::connected = (wifi_status == WL_CONNECTED);
  if (Wifi::connected) {
    static uint8_t rssi_cnt = 0;
    if (++rssi_cnt >= 60) {
      rssi_cnt = 0;
      Wifi::rssi = WiFi.RSSI();
      // Refresh IP cache once a minute to catch DHCP renewals.
      Wifi::local_ip = WiFi.localIP();
    }
    if (!prev) {
      Wifi::local_ip = WiFi.localIP();
      strncpy(Wifi::ssid, WiFi.SSID().c_str(), sizeof(Wifi::ssid) - 1);
      Wifi::ssid[sizeof(Wifi::ssid) - 1] = '\0';
      Wifi::rssi = WiFi.RSSI();
      Wifi::connected_at_ms = now;   // start the stability timer
    }
  }

  if (!Wifi::disabled) {
    if (wifi_status != WL_CONNECTED) {
      if (was_connected) {
        was_connected = false;
        lost_at = now;
        Wifi::connected_at_ms = 0;  // disconnect resets stability
        Log::console(PSTR("WiFi: Connection lost"));
      }
      unsigned long down_seconds = (now - lost_at) / 1000;
      if (down_seconds > 0 && down_seconds % 30 == 0) {
        Log::console(PSTR("WiFi: Attempting reconnect (%lus)"), down_seconds);
        WiFi.reconnect();
      }
      if (down_seconds > 300) {
        Log::console(PSTR("WiFi: Down for 5 minutes, rebooting"));
        DeviceInfo::safeRestart(100);
      }
    } else if (!was_connected) {
      was_connected = true;
      if (lost_at > 0) {
        unsigned long down_seconds = (now - lost_at) / 1000;
        Log::console(PSTR("WiFi: Reconnected after %lus"), down_seconds);
      }
    }
  }

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
      IPAddress ipa = WiFi.localIP();
      Log::console(PSTR("WiFi: IP: %u.%u.%u.%u"),
                   ipa[0], ipa[1], ipa[2], ipa[3]);
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
  // WiFi.SSID() can lie right after mode change; query the SDK directly.
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK &&
      conf.sta.ssid[0] != '\0') {
    return true;
  }
  delay(50);   // belt-and-braces for SDK timing edge cases
  return WiFi.SSID().length() > 0;
#else
  return WiFi.SSID().length() > 0;
#endif
}

void Wifi::clearSavedCreds() {
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

// Parse up to nFields newline-delimited records from f into buf, NUL
// each. fields[i] points into buf. No heap (vs String/readStringUntil).
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

// On success, removes backup so next boot uses SDK NVS creds.
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
  // Don't clobber a pending backup - we'd lose last-known-good.
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
  // Gateway must share the subnet, else ARP can't reach it. The
  // DNS probe doesn't catch the case where DNS lives on the local subnet.
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
  Log::console(PSTR("WiFi: Static %u.%u.%u.%u gw=%u.%u.%u.%u sn=%u.%u.%u.%u dns=%u.%u.%u.%u"),
               ip[0], ip[1], ip[2], ip[3],
               gw[0], gw[1], gw[2], gw[3],
               sn[0], sn[1], sn[2], sn[3],
               dns[0], dns[1], dns[2], dns[3]);
  return true;
}

void Wifi::onImprovCreds(const char* ssid, const char* pass) {
  if (!ssid) return;
  strncpy(s_portalSsid, ssid, sizeof(s_portalSsid) - 1);
  s_portalSsid[sizeof(s_portalSsid) - 1] = '\0';
  strncpy(s_portalPass, pass ? pass : "", sizeof(s_portalPass) - 1);
  s_portalPass[sizeof(s_portalPass) - 1] = '\0';
  s_portalGotCreds = true;
}

bool Wifi::connectOrPortal() {
  if (Wifi::hasSavedCreds()) {
    WiFi.mode(WIFI_STA);
    Wifi::applyRuntimeNetPrefs();
    bool staticOn = Wifi::applyStaticConfig();
    WiFi.begin();                            // uses NVS-stored creds
    if (waitForWifi(30000)) {
      // Probe-and-revert: associated radio-side but maybe unreachable
      // (bad gateway/subnet). Backup is left by /wifisave; tick clears
      // it after a 5-min stable boot.
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
          DeviceInfo::safeRestart(500);
        }
      }
      return true;
    }
    Log::console(PSTR("WiFi: Saved creds failed to associate"));
    if (tryRestoreBackup()) return true;
  }

  // Captive setup AP. Static config (if any) carries over to picked SSID.
  s_portalGotCreds = false;
  auto* portal = new EGPortal();
  portal->setTitle(THING_NAME);
  // Heap-alloc'd for the portal's lifetime - a static buffer would burn
  // 256 B BSS permanently on this setup-only path.
  char* notice = (char*)malloc(256);
  if (notice) {
    snprintf_P(notice, 256,
      PSTR("<p style='margin:0 0 1em;background:#eef;padding:.6em .8em;"
           "border-radius:.3em;font-size:.85em;line-height:1.4'>"
           "After saving, find this device at <b>%s.local</b> on your network.</p>"),
      DeviceInfo::hostname());
    portal->setNotice(notice);
  }
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
    improvSerial.poll();
    delay(10);
    yield();
  }
  portal->end();
  delete portal;
  free(notice);  // safe on nullptr
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  Wifi::applyRuntimeNetPrefs();
  Wifi::applyStaticConfig();
  WiFi.begin(s_portalSsid, s_portalPass);
  return waitForWifi(30000);
}

// ---------- Wi-Fi runtime prefs ----------
// Hidden tunables under /pref?group=wifi&key=*.

class WifiPrefs : public EGModule {
public:
  const char* name() override { return "wifi"; }
  uint8_t display_order() override { return 0; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static WifiPrefs wifiprefs;
EG_REGISTER_MODULE(wifiprefs)

#ifdef ESP8266
#define WIFI_SLEEP_DEFAULT "1"
#else
#define WIFI_SLEEP_DEFAULT "2"
#endif

static const EGPref WIFI_PREF_ITEMS[] = {
  {"sleep",    nullptr, nullptr, WIFI_SLEEP_DEFAULT, nullptr, 0, 2,  0, EGP_UINT,   EGP_HIDDEN},
  {"tx_power", nullptr, nullptr, "0",                nullptr, 0, 20, 0, EGP_UINT,   EGP_HIDDEN},
  {"country",  nullptr, nullptr, "",                 nullptr, 0, 0,  3, EGP_STRING, EGP_HIDDEN},
  {"phy_mode", nullptr, nullptr, "0",                nullptr, 0, 3,  0, EGP_UINT,   EGP_HIDDEN},
};

static const EGPrefGroup WIFI_PREF_GROUP = {
  "wifi", "Wi-Fi", 1,
  WIFI_PREF_ITEMS,
  sizeof(WIFI_PREF_ITEMS) / sizeof(WIFI_PREF_ITEMS[0]),
};

const EGPrefGroup* WifiPrefs::prefs_group() { return &WIFI_PREF_GROUP; }

static void apply_wifi_sleep(uint8_t mode) {
  // Always apply: the SDK persists sleep mode in NVS, so skipping the call
  // on mode=1 leaves a previously-applied non-default in place across reboot.
#ifdef ESP8266
  switch (mode) {
    case 0:  WiFi.setSleepMode(WIFI_LIGHT_SLEEP); break;
    case 2:  WiFi.setSleepMode(WIFI_NONE_SLEEP);  break;
    case 1:
    default: WiFi.setSleepMode(WIFI_MODEM_SLEEP); break;
  }
#else
  switch (mode) {
    case 0:  WiFi.setSleep(WIFI_PS_MAX_MODEM); break;
    case 2:  WiFi.setSleep(WIFI_PS_NONE);      break;
    case 1:
    default: WiFi.setSleep(WIFI_PS_MIN_MODEM); break;
  }
#endif
}

static void apply_wifi_tx_power(uint8_t dBm) {
  if (dBm == 0) return;
#ifdef ESP8266
  WiFi.setOutputPower((float)dBm);
#else
  esp_wifi_set_max_tx_power((int8_t)(dBm * 4));
#endif
}

static void apply_wifi_country(const char* code) {
  if (!code || !code[0]) return;
#ifdef ESP8266
  wifi_country_t c = {0};
  strncpy(c.cc, code, 2);
  c.cc[2] = '\0';
  c.schan  = 1;
  c.nchan  = 13;
  c.policy = WIFI_COUNTRY_POLICY_MANUAL;
  wifi_set_country(&c);
#else
  esp_wifi_set_country_code(code, true);
#endif
}

static void apply_wifi_phy_mode(uint8_t mode) {
  if (mode == 0) return;
#ifdef ESP8266
  WiFiPhyMode_t m;
  switch (mode) {
    case 1:  m = WIFI_PHY_MODE_11B; break;
    case 2:  m = WIFI_PHY_MODE_11G; break;
    case 3:  m = WIFI_PHY_MODE_11N; break;
    default: return;
  }
  WiFi.setPhyMode(m);
#else
  uint8_t mask;
  switch (mode) {
    case 1:  mask = WIFI_PROTOCOL_11B; break;
    case 2:  mask = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G; break;
    case 3:  mask = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N; break;
    default: return;
  }
  esp_wifi_set_protocol(WIFI_IF_STA, mask);
#endif
}

void WifiPrefs::on_prefs_loaded() {
  // Sleep is safe pre-init; the rest deferred to Wifi::applyRuntimeNetPrefs.
  apply_wifi_sleep((uint8_t)EGPrefs::getUInt("wifi", "sleep"));
}

void Wifi::applyRuntimeNetPrefs() {
  apply_wifi_tx_power((uint8_t)EGPrefs::getUInt  ("wifi", "tx_power"));
  apply_wifi_country (         EGPrefs::getString("wifi", "country"));
  apply_wifi_phy_mode((uint8_t)EGPrefs::getUInt  ("wifi", "phy_mode"));
}
