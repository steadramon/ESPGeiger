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
#include <esp_wifi.h>
#endif
#include <EGPortal.h>
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModule.h"
#include "Corium/ImprovSerial/ImprovSerial.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Fs.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Util/StringUtil.h"

EG_PSTR(K_SLEEP,    "sleep");
EG_PSTR(K_TX_POWER, "tx_power");
EG_PSTR(K_COUNTRY,  "country");
EG_PSTR(K_PHY_MODE, "phy_mode");

// SSID\nPSK\n
static const char WIFI_BACKUP_P[] PROGMEM = "/wifi_backup";
#define WIFI_BACKUP_PATH FPSTR(WIFI_BACKUP_P)

// static_ip\nip\ngw\nsn\ndns\n
static const char NET_BACKUP_P[] PROGMEM = "/net_backup";
#define NET_BACKUP_PATH FPSTR(NET_BACKUP_P)

static_assert(WIFI_SSID_MAX == EGPORTAL_SSID_MAX, "one ssid max");

// save to WiFi.begin() only
struct Creds { char ssid[EGPORTAL_SSID_MAX + 1]; char pass[EGPORTAL_PASS_MAX + 1]; };
static Creds* s_creds = nullptr;

static bool          s_inPortal       = false;
static volatile bool s_portalGotCreds = false;
static volatile bool s_portalDoReset  = false;

namespace Wifi {
  bool disabled = false;
  bool offline_forced = false;
  bool connected = false;
  IPAddress local_ip;
  char ssid[WIFI_SSID_MAX + 1] = "";
  int16_t rssi = 0;
  uint32_t connected_at_ms = 0;
  bool backup_grace_done = false;

  void check_backup_state() {
    backup_grace_done = !Fs::exists(WIFI_BACKUP_PATH)
                     && !Fs::exists(NET_BACKUP_PATH);
  }

  size_t formatIP(char* buf, size_t cap, const IPAddress& a) {
    return snprintf_P(buf, cap, PSTR("%u.%u.%u.%u"), a[0], a[1], a[2], a[3]);
  }

  size_t formatIP(char* buf, size_t cap) {
    return formatIP(buf, cap, local_ip);
  }
}

static bool was_connected = false;
static unsigned long lost_at = 0;
static unsigned long s_offline_retry_at = 0;
static const uint32_t WIFI_OFFLINE_RETRY_MS = 120000UL;
static const uint32_t PORTAL_RETRY_MS = 600000UL;   // 10 min
#ifndef WIFI_DOWN_MAX_S
#define WIFI_DOWN_MAX_S 3600
#endif
static const uint32_t WIFI_OFFLINE_MAX_MS = WIFI_DOWN_MAX_S * 1000UL;

static void refresh_link_cache(bool just_connected, unsigned long now) {
  static uint8_t rssi_cnt = 0;
  if (++rssi_cnt >= 60) {
    rssi_cnt = 0;
    Wifi::rssi = WiFi.RSSI();
    Wifi::local_ip = WiFi.localIP();
  }
  if (just_connected) {
    Wifi::local_ip = WiFi.localIP();
    strlcpy(Wifi::ssid, WiFi.SSID().c_str(), sizeof(Wifi::ssid));
    Wifi::rssi = WiFi.RSSI();
    Wifi::connected_at_ms = now;
  }
}

static void track_outage(unsigned long now) {
  if (was_connected) {
    was_connected = false;
    lost_at = now;
    Wifi::connected_at_ms = 0;
    Log::console(PSTR("WiFi: Connection lost"));
  } else if (lost_at == 0) {
    lost_at = now;
  }
  unsigned long down_seconds = (now - lost_at) / 1000;
  if (down_seconds > 0 && down_seconds % 30 == 0) {
    Log::console(PSTR("WiFi: Attempting reconnect (%lus)"), down_seconds);
    WiFi.reconnect();
  }
  if (down_seconds > WIFI_DOWN_MAX_S) {
    Log::console(PSTR("WiFi: Down for %lus, rebooting"), down_seconds);
    Lifecycle::request_restart(now, 100);
  }
}

static void note_reconnected(unsigned long now) {
  was_connected = true;
  if (lost_at > 0) {
    unsigned long down_seconds = (now - lost_at) / 1000;
    Log::console(PSTR("WiFi: Reconnected after %lus"), down_seconds);
  }
}

static void reprobe_saved_network(unsigned long now) {
  if (Wifi::connected) {
    Log::console(PSTR("WiFi: Saved network back, rebooting to reconnect"));
    Lifecycle::request_restart(now, 500);
  } else if (now > WIFI_OFFLINE_MAX_MS) {
    Log::console(PSTR("WiFi: Offline %lus, rebooting to retry"), now / 1000);
    Lifecycle::request_restart(now, 500);
  } else if (s_offline_retry_at == 0) {
    s_offline_retry_at = now + WIFI_OFFLINE_RETRY_MS;
  } else if ((long)(now - s_offline_retry_at) >= 0) {
    s_offline_retry_at = now + WIFI_OFFLINE_RETRY_MS;
    Log::console(PSTR("WiFi: Offline - re-probing saved network"));
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }
}

static void clear_backups_once_stable() {
  Wifi::backup_grace_done = true;
  if (Fs::remove(WIFI_BACKUP_PATH)) {
    Log::console(PSTR("WiFi: New creds stable for 5 min, backup cleared"));
  }
  if (Fs::remove(NET_BACKUP_PATH)) {
    Log::console(PSTR("WiFi: Static IP stable for 5 min, net backup cleared"));
  }
}

void Wifi::tick(unsigned long now) {
  wl_status_t wifi_status = WiFi.status();
  bool prev = Wifi::connected;
  Wifi::connected = (wifi_status == WL_CONNECTED);
  if (Wifi::connected) refresh_link_cache(!prev, now);

  if (!Wifi::disabled) {
    if (wifi_status != WL_CONNECTED) track_outage(now);
    else if (!was_connected) note_reconnected(now);
  } else if (!Wifi::offline_forced) {
    reprobe_saved_network(now);
  }

  if (!backup_grace_done && Wifi::connected && now > 300000UL) clear_backups_once_stable();
}

// Boot association

static bool waitForWifi(uint32_t timeoutMs) {
  uint32_t start = fast_millis();
  while ((fast_millis() - start) < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ipa = WiFi.localIP();
      Log::console(PSTR("WiFi: IP: %u.%u.%u.%u"),
                   ipa[0], ipa[1], ipa[2], ipa[3]);
      return true;
    }
    improvSerial.poll();
    delay(100);
    yield();
  }
  return false;
}

bool Wifi::hasSavedCreds() {
  WiFi.mode(WIFI_STA);
#ifdef ESP32
  // WiFi.SSID() can lie here
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK &&
      conf.sta.ssid[0] != '\0') {
    return true;
  }
  delay(50);   // SDK timing
  return WiFi.SSID().length() > 0;
#else
  return WiFi.SSID().length() > 0;
#endif
}

void Wifi::clearSavedCreds() {
#ifdef ESP8266
  WiFi.persistent(true);
  WiFi.disconnect(true);          // also clears creds
#else
  WiFi.persistent(true);
  WiFi.disconnect(true, true);    // disable + eraseAP
#endif
  Fs::remove(WIFI_BACKUP_PATH);
  Fs::remove(NET_BACKUP_PATH);
  Log::console(PSTR("WiFi: SDK creds cleared"));
}

// SDK fields aren't NUL-ended at full length
static bool sdk_creds(Creds& c) {
#ifdef ESP32
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
  memcpy(c.ssid, conf.sta.ssid, EGPORTAL_SSID_MAX);
  memcpy(c.pass, conf.sta.password, EGPORTAL_PASS_MAX);
#else
  struct station_config conf;
  if (!wifi_station_get_config(&conf)) return false;
  memcpy(c.ssid, conf.ssid, EGPORTAL_SSID_MAX);
  memcpy(c.pass, conf.password, EGPORTAL_PASS_MAX);
#endif
  c.ssid[EGPORTAL_SSID_MAX] = '\0';
  c.pass[EGPORTAL_PASS_MAX] = '\0';
  return c.ssid[0] != '\0';
}

bool Wifi::saveBackupCreds() {
  Creds c;
  if (!sdk_creds(c)) return false;
  if (!Fs::begin()) return false;
  File f = Fs::open_w(WIFI_BACKUP_PATH);
  if (!f) return false;
  f.print(c.ssid);
  f.print('\n');
  f.print(c.pass);
  f.print('\n');
  f.close();
  Log::console(PSTR("WiFi: Backup saved (ssid=%s)"), c.ssid);
  return true;
}

static uint8_t readLines(File& f, char* buf, size_t bufLen,
                         char** fields, uint8_t nFields) {
  if (bufLen == 0 || nFields == 0) return 0;
  int n = f.read((uint8_t*)buf, bufLen - 1);
  if (n < 0) n = 0;
  buf[n] = '\0';
  return split_lines(buf, fields, nFields);
}

static bool tryRestoreBackup() {
  File f = Fs::open_r(WIFI_BACKUP_PATH);
  if (!f) return false;
  // SSID\nPSK\n
  char raw[EGPORTAL_SSID_MAX + EGPORTAL_PASS_MAX + 3];
  char* fields[2];
  readLines(f, raw, sizeof(raw), fields, 2);
  f.close();
  const char* ssid = fields[0];
  const char* pass = fields[1];
  if (!*ssid) {
    Fs::remove(WIFI_BACKUP_PATH);
    return false;
  }
  Log::console(PSTR("WiFi: Restoring backup creds (ssid=%s)"), ssid);
  WiFi.persistent(true);
  WiFi.begin(ssid, pass);
  bool ok = waitForWifi(30000);
  if (ok) {
    Fs::remove(WIFI_BACKUP_PATH);
    Log::console(PSTR("WiFi: Reverted to backup creds"));
  }
  return ok;
}

bool Wifi::saveNetBackup() {
  if (!Fs::begin()) return false;
  // keep last good
  if (Fs::exists(NET_BACKUP_PATH)) return false;
  File f = Fs::open_w(NET_BACKUP_PATH);
  if (!f) return false;
  f.printf("%s\n%s\n%s\n%s\n%s\n",
           EGPrefs::getBool("net", EG_K_STATIC_IP) ? "1" : "0",
           EGPrefs::getString("net", EG_K_IP),
           EGPrefs::getString("net", EG_K_GW),
           EGPrefs::getString("net", EG_K_SN),
           EGPrefs::getString("net", EG_K_DNS));
  f.close();
  return true;
}

bool Wifi::probeReachability() {
  IPAddress dummy;
  return WiFi.hostByName("pool.ntp.org", dummy);  // NOLINT(dram)
}

bool Wifi::restoreNetBackup() {
  File f = Fs::open_r(NET_BACKUP_PATH);
  if (!f) return false;
  // 5 IPs + newlines
  char raw[96];
  char* fields[5];
  readLines(f, raw, sizeof(raw), fields, 5);
  f.close();
  EGPrefs::put("net", EG_K_STATIC_IP, fields[0]);
  EGPrefs::put("net", EG_K_IP,     fields[1]);
  EGPrefs::put("net", EG_K_GW,     fields[2]);
  EGPrefs::put("net", EG_K_SN,     fields[3]);
  EGPrefs::put("net", EG_K_DNS,    fields[4]);
  EGPrefs::commit();
  Fs::remove(NET_BACKUP_PATH);
  return true;
}

void Wifi::dropNetBackup() { Fs::remove(NET_BACKUP_PATH); }

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
  if (((uint32_t)ip & (uint32_t)sn) != ((uint32_t)gw & (uint32_t)sn)) {
    if (errbuf) snprintf_P(errbuf, errlen,
      PSTR("gateway %s outside subnet %s/%s"), s_gw, s_ip, s_sn);
    return false;
  }
  return true;
}

bool Wifi::applyStaticConfig() {
  if (!EGPrefs::getBool("net", EG_K_STATIC_IP)) return false;
  const char* s_ip  = EGPrefs::getString("net", EG_K_IP);
  const char* s_gw  = EGPrefs::getString("net", EG_K_GW);
  const char* s_sn  = EGPrefs::getString("net", EG_K_SN);
  const char* s_dns = EGPrefs::getString("net", EG_K_DNS);
  char err[96];
  if (!Wifi::validateStatic(s_ip, s_gw, s_sn, err, sizeof(err))) {
    Log::console(PSTR("WiFi: Static IP rejected (%s); using DHCP"), err);
    return false;
  }
  IPAddress ip, gw, sn, dns;
  ip.fromString(s_ip);
  gw.fromString(s_gw);
  sn.fromString(s_sn);
  if (!dns.fromString(s_dns)) dns = gw;
  WiFi.config(ip, gw, sn, dns);
  Log::console(PSTR("WiFi: Static %u.%u.%u.%u gw=%u.%u.%u.%u sn=%u.%u.%u.%u dns=%u.%u.%u.%u"),
               ip[0], ip[1], ip[2], ip[3],
               gw[0], gw[1], gw[2], gw[3],
               sn[0], sn[1], sn[2], sn[3],
               dns[0], dns[1], dns[2], dns[3]);
  return true;
}

// too long is refused, a cut one would never join
bool Wifi::holdCreds(const char* ssid, const char* pass) {
  if (!ssid) return false;
  if (!pass) pass = "";
  size_t sl = strlen(ssid), pl = strlen(pass);
  if (sl > EGPORTAL_SSID_MAX || pl > EGPORTAL_PASS_MAX) return false;
  if (!s_creds) s_creds = (Creds*)malloc(sizeof(Creds));
  if (!s_creds) return false;
  memcpy(s_creds->ssid, ssid, sl + 1);
  memcpy(s_creds->pass, pass, pl + 1);
  return true;
}

const char* Wifi::heldSsid() { return s_creds ? s_creds->ssid : ""; }

bool Wifi::beginHeld() {
  if (!s_creds) return false;
  WiFi.persistent(true);
  WiFi.begin(s_creds->ssid, s_creds->pass);
  free(s_creds);
  s_creds = nullptr;
  return true;
}

// outside the portal Improv joins by itself
void Wifi::onImprovCreds(const char* ssid, const char* pass) {
  if (s_inPortal && Wifi::holdCreds(ssid, pass)) s_portalGotCreds = true;
}

static void reset_wifi_tx_power_default();

// retries, slow AP after a power blip
static bool join_saved_network() {
  bool associated = false;
  for (uint8_t attempt = 0; attempt < 3 && !associated; attempt++) {
    if (attempt > 0) Log::console(PSTR("WiFi: Retrying saved network (%u/2)"), attempt);
    WiFi.begin();                          // uses NVS-stored creds
    associated = waitForWifi(attempt == 0 ? 30000 : 15000);
  }
  return associated;
}

// associated but may not route
static void probe_new_static_config(bool staticOn) {
  bool hasBackup = Fs::exists(NET_BACKUP_PATH);
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
}

// caller frees
static char* setup_portal(EGPortal* portal) {
  portal->setTitle(THING_NAME);
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
    if (Wifi::holdCreds(ssid, pw)) s_portalGotCreds = true;
  });
  portal->onReset([](void*) { s_portalDoReset = true; });
  portal->begin(DeviceInfo::hostname());
  reset_wifi_tx_power_default();   // after begin(), full power
  Log::console(PSTR("WiFi: Setup portal up on AP %s"), DeviceInfo::hostname());
  return notice;
}

static void portal_factory_reset(EGPortal* portal) {
  // let the page flush
  unsigned long flush_until = fast_millis() + 1200;
  while ((long)(flush_until - fast_millis()) > 0) {
    portal->loop();
    delay(10);
    yield();
  }
  Log::console(PSTR("WiFi: Factory reset from setup portal"));
  DeviceInfo::factoryReset();
  DeviceInfo::safeRestart(200);
}

// hasSavedCreds() here drops the AP
static void run_portal(EGPortal* portal, bool haveCreds) {
  unsigned long portal_idle_since = fast_millis();
  while (!s_portalGotCreds) {
    portal->loop();
    improvSerial.poll();
    delay(10);
    yield();
    if (s_portalDoReset) portal_factory_reset(portal);
    if (!haveCreds) continue;   // first setup, wait forever
    if (WiFi.softAPgetStationNum() > 0) portal_idle_since = fast_millis();
    if ((fast_millis() - portal_idle_since) >= PORTAL_RETRY_MS ||
        fast_millis() >= WIFI_OFFLINE_MAX_MS) {
      Log::console(PSTR("WiFi: Portal timeout, rebooting to retry saved network"));
      DeviceInfo::safeRestart(500);
    }
  }
}

bool Wifi::connectOrPortal() {
  bool haveCreds = Wifi::hasSavedCreds();
  if (haveCreds) {
    WiFi.mode(WIFI_STA);
    Wifi::applyRuntimeNetPrefs();
    bool staticOn = Wifi::applyStaticConfig();
    if (join_saved_network()) {
      probe_new_static_config(staticOn);
      return true;
    }
    Log::console(PSTR("WiFi: Saved creds failed to associate"));
    if (tryRestoreBackup()) return true;
#ifdef APP_OFFLINE_OK
    return false;
#endif
  }

  s_portalGotCreds = false;
  auto* portal = new EGPortal();
  if (!portal) {
    Log::console(PSTR("WiFi: Portal alloc failed, rebooting"));
    DeviceInfo::safeRestart(500);
    return false;
  }
  char* notice = setup_portal(portal);
  s_inPortal = true;
  run_portal(portal, haveCreds);
  s_inPortal = false;
  portal->end();
  delete portal;
  free(notice);
  delay(500);

  WiFi.mode(WIFI_STA);
  Wifi::applyRuntimeNetPrefs();
  Wifi::applyStaticConfig();
  if (!Wifi::beginHeld()) return false;
  return waitForWifi(30000);
}

class WifiPrefs : public EGModule {
public:
  const char* name() override { return PSTR("wifi"); }
  bool core_module() override { return true; }
  uint8_t display_order() override { return 0; }
  const EGPrefGroup* prefs_group() override;
  void on_prefs_loaded() override;
};

static WifiPrefs wifiprefs;
EG_REGISTER_MODULE(wifiprefs)

#define WIFI_SLEEP_DEFAULT "1"

static const EGPref WIFI_PREF_ITEMS[] PROGMEM = {
  {K_SLEEP,    nullptr, nullptr, WIFI_SLEEP_DEFAULT, nullptr, 0, 2,  0, EGP_UINT,   EGP_HIDDEN},
  {K_TX_POWER, nullptr, nullptr, "0",                nullptr, 0, 20, 0, EGP_UINT,   EGP_HIDDEN},
  {K_COUNTRY,  nullptr, nullptr, "",                 nullptr, 0, 0,  3, EGP_STRING, EGP_HIDDEN},
  {K_PHY_MODE, nullptr, nullptr, "0",                nullptr, 0, 3,  0, EGP_UINT,   EGP_HIDDEN},
};

static const EGPrefGroup WIFI_PREF_GROUP PROGMEM = {
  "wifi", "Wi-Fi", 1,
  WIFI_PREF_ITEMS,
  sizeof(WIFI_PREF_ITEMS) / sizeof(WIFI_PREF_ITEMS[0]),
};

const EGPrefGroup* WifiPrefs::prefs_group() { return &WIFI_PREF_GROUP; }

static int8_t s_rx_sleep = -1;   // UdpRx's, wins over the pref

static void apply_wifi_sleep() {
  // always, SDK keeps it in NVS
  bool rx = s_rx_sleep >= 0;
  uint8_t mode = rx ? (uint8_t)s_rx_sleep : (uint8_t)EGPrefs::getUInt("wifi", K_SLEEP);
#ifdef ESP8266
  // multicast: listen interval 1, default 3 drops frames
  uint8_t listen = rx ? 1 : 0;
  switch (mode) {
    case 0:  WiFi.setSleepMode(WIFI_LIGHT_SLEEP, listen); break;
    case 2:  WiFi.setSleepMode(WIFI_NONE_SLEEP);  break;
    case 1:
    default: WiFi.setSleepMode(WIFI_MODEM_SLEEP, listen); break;
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

#define WIFI_TX_POWER_MIN_DBM 2

static void apply_wifi_tx_power(uint8_t dBm) {
  if (dBm == 0) return;   // 0 = leave at default (full)
  if (dBm < WIFI_TX_POWER_MIN_DBM) {
    Log::console(PSTR("WiFi: tx_power %u too low, using %u dBm"), dBm, WIFI_TX_POWER_MIN_DBM);
    dBm = WIFI_TX_POWER_MIN_DBM;
  }
#ifdef ESP8266
  WiFi.setOutputPower((float)dBm);
#else
  esp_wifi_set_max_tx_power((int8_t)(dBm * 4));
#endif
}

static void reset_wifi_tx_power_default() {
#ifdef ESP8266
  WiFi.setOutputPower(20.5f);
#else
  esp_wifi_set_max_tx_power(80);
#endif
}

static void apply_wifi_country(const char* code) {
  if (!code || !code[0]) return;
#ifdef ESP8266
  wifi_country_t c = {0};
  strlcpy(c.cc, code, sizeof(c.cc));
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
  // rest needs WiFi.mode() first
  apply_wifi_sleep();
}

void Wifi::rx_sleep(int8_t mode) {
  s_rx_sleep = mode;
  apply_wifi_sleep();
}

void Wifi::applyRuntimeNetPrefs() {
  apply_wifi_tx_power((uint8_t)EGPrefs::getUInt  ("wifi", K_TX_POWER));
  apply_wifi_country (         EGPrefs::getString("wifi", K_COUNTRY));
  apply_wifi_phy_mode((uint8_t)EGPrefs::getUInt  ("wifi", K_PHY_MODE));
}

int Wifi::txPowerQdbm() {
#ifdef ESP8266
  uint8_t dBm = (uint8_t)EGPrefs::getUInt("wifi", K_TX_POWER);
  if (dBm == 0) return 82;   // core default 20.5 dBm
  if (dBm < WIFI_TX_POWER_MIN_DBM) dBm = WIFI_TX_POWER_MIN_DBM;
  return (int)dBm * 4;
#else
  int8_t q = 0;
  if (esp_wifi_get_max_tx_power(&q) != ESP_OK) return -1;
  return (int)q;
#endif
}

const char* Wifi::phyModeStr() {
#ifdef ESP8266
  switch (WiFi.getPhyMode()) {
    case WIFI_PHY_MODE_11B: return PSTR("11b");
    case WIFI_PHY_MODE_11G: return PSTR("11g");
    case WIFI_PHY_MODE_11N: return PSTR("11n");
    default:                return PSTR("?");
  }
#else
  uint8_t mask = 0;
  if (esp_wifi_get_protocol(WIFI_IF_STA, &mask) != ESP_OK) return PSTR("?");
  if (mask & WIFI_PROTOCOL_11N) return PSTR("11n");
  if (mask & WIFI_PROTOCOL_11G) return PSTR("11g");
  if (mask & WIFI_PROTOCOL_11B) return PSTR("11b");
  return PSTR("?");
#endif
}

const char* Wifi::sleepModeStr() {
#ifdef ESP8266
  switch (WiFi.getSleepMode()) {
    case WIFI_NONE_SLEEP:  return PSTR("None");
    case WIFI_LIGHT_SLEEP: return PSTR("Light");
    case WIFI_MODEM_SLEEP: return PSTR("Modem");
    default:               return PSTR("?");
  }
#else
  return WiFi.getSleep() ? PSTR("Modem") : PSTR("None");
#endif
}
