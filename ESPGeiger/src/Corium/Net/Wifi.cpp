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
#include "WifiLink.h"
#include "WifiSetup.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <user_interface.h>
#else
#include <WiFi.h>
#include <esp_wifi.h>
#endif
#include <EGPortal.h>
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Module.h"
#include "Corium/Console/Console.h"
#include "Corium/Module/Registry.h"
#include "Corium/NTP/NTP.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Fs.h"
#include "Corium/Util/StringUtil.h"

CORIUM_PSTR(K_SLEEP,    "sleep");
CORIUM_PSTR(K_TX_POWER, "tx_power");
CORIUM_PSTR(K_COUNTRY,  "country");
CORIUM_PSTR(K_PHY_MODE, "phy_mode");
CORIUM_PSTR(K_MODE,     "mode");
CORIUM_PSTR(K_AP_HIDE,  "ap_hide");

static const uint8_t MODE_OFF = 1;
static const uint8_t MODE_AP  = 2;
static const size_t AP_PASS_MIN = 8;   // wpa2

static_assert(WIFI_SSID_MAX == EGPORTAL_SSID_MAX, "one ssid max");

// save to WiFi.begin() only
struct Creds { char ssid[EGPORTAL_SSID_MAX + 1]; char pass[EGPORTAL_PASS_MAX + 1]; };
static Creds* s_creds = nullptr;

static bool s_in_portal = false;
static bool s_joining = false;
static bool s_got_creds = false;
static bool s_reset = false;

namespace corium {
namespace Wifi {
  bool disabled = false;
  bool offlineForced = false;

  size_t formatIP(char* buf, size_t cap, const IPAddress& a) {
    return snprintf_P(buf, cap, PSTR("%u.%u.%u.%u"), a[0], a[1], a[2], a[3]);
  }

  size_t formatIP(char* buf, size_t cap) {
    return formatIP(buf, cap, local_ip);
  }
}
}  // namespace corium

static const uint32_t PORTAL_RETRY_MS = 600000UL;   // 10 min
static const uint32_t JOIN_WAIT_MS = 30000;
static const uint32_t JOIN_RETRY_WAIT_MS = 15000;
#define JOIN_RETRIES 2

// Boot association

static bool wait_wifi(uint32_t timeout_ms) {
  uint32_t start = corium::fastMillis();
  while ((corium::fastMillis() - start) < timeout_ms && !corium::Wifi::offlineForced) {
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ipa = WiFi.localIP();
      corium::Log::console(PSTR("WiFi: IP: %u.%u.%u.%u"),
                   ipa[0], ipa[1], ipa[2], ipa[3]);
      return true;
    }
    console.pollBoot();
    delay(100);
    yield();
  }
  return false;
}

bool corium::Wifi::hasSavedCreds() {
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

bool corium::Wifi::clearSavedCreds() {
#ifdef ESP8266
  WiFi.persistent(true);
  WiFi.disconnect(true);          // also clears creds, any mode
  WiFi.persistent(false);
  struct station_config c;
  bool ok = wifi_station_get_config_default(&c) && !c.ssid[0];
#else
  // disconnect(true, true) erases only with STA up, this in any mode
  bool ok = WiFi.eraseAP();
#endif
  corium::Fs::remove(WIFI_BACKUP_PATH);
  corium::Fs::remove(NET_BACKUP_PATH);
  corium::Log::console(ok ? PSTR("WiFi: SDK creds cleared") : PSTR("WiFi: SDK creds NOT cleared"));
  return ok;
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

static bool write_backup(const Creds& c) {
  if (!corium::Fs::begin()) return false;
  // 0, len, ssid, len, pass; an ssid can hold \n
  uint8_t raw[3 + EGPORTAL_SSID_MAX + EGPORTAL_PASS_MAX];
  uint8_t sl = strlen(c.ssid), pl = strlen(c.pass);
  raw[0] = 0;
  raw[1] = sl;
  memcpy(raw + 2, c.ssid, sl);
  raw[2 + sl] = pl;
  memcpy(raw + 3 + sl, c.pass, pl);
  return corium::Fs::writeWhole(WIFI_BACKUP_PATH, raw, 3 + sl + pl);
}

bool corium::Wifi::saveBackupCreds() {
  Creds c;
  if (!sdk_creds(c)) return false;
  if (!write_backup(c)) {
    corium::Log::console(PSTR("WiFi: Backup not saved"));
    return false;
  }
  corium::Log::console(PSTR("WiFi: Backup saved (ssid=%s)"), c.ssid);
  return true;
}

static uint8_t read_lines(File& f, char* buf, size_t cap,
                         char** fields, uint8_t count) {
  if (cap == 0 || count == 0) return 0;
  int n = f.read((uint8_t*)buf, cap - 1);
  if (n < 0) n = 0;
  buf[n] = '\0';
  return splitLines(buf, fields, count);
}

// 0.13 wrote SSID\nPSK\n, never a 0 first
static void split_backup(char* raw, size_t n, char** fields) {
  if (n == 0 || raw[0] != 0) {
    splitLines(raw, fields, 2);
    return;
  }
  fields[0] = fields[1] = raw + n;
  size_t sl = (uint8_t)raw[1], pl_at = 2 + sl, pass_at = pl_at + 1;
  if (n < pass_at) return;
  size_t pl = (uint8_t)raw[pl_at];
  if (n < pass_at + pl) return;
  raw[pl_at] = '\0';
  raw[pass_at + pl] = '\0';
  fields[0] = raw + 2;
  fields[1] = raw + pass_at;
}

static bool restore_backup() {
  File f = corium::Fs::openR(WIFI_BACKUP_PATH);
  if (!f) return false;
  char raw[EGPORTAL_SSID_MAX + EGPORTAL_PASS_MAX + 4];
  int n = f.read((uint8_t*)raw, sizeof(raw) - 1);
  f.close();
  if (n < 0) n = 0;
  raw[n] = '\0';
  char* fields[2];
  split_backup(raw, n, fields);
  const char* ssid = fields[0];
  const char* pass = fields[1];
  // none, or one no join takes (a bad file)
  if (!*ssid || strlen(ssid) > EGPORTAL_SSID_MAX || strlen(pass) > EGPORTAL_PASS_MAX) {
    corium::Fs::remove(WIFI_BACKUP_PATH);
    return false;
  }
  corium::Log::console(PSTR("WiFi: Restoring backup creds (ssid=%s)"), ssid);
  corium::Wifi::beginSaved(ssid, pass);
  bool ok = wait_wifi(JOIN_WAIT_MS);
  if (ok) {
    corium::Fs::remove(WIFI_BACKUP_PATH);
    corium::Log::console(PSTR("WiFi: Reverted to backup creds"));
  }
  return ok;
}

// 5 IPs + newlines
#define NET_LINES_MAX 96

static void net_lines(char* buf, size_t len) {
  snprintf_P(buf, len, PSTR("%s\n%s\n%s\n%s\n%s\n"),
             corium::Prefs::getBool("net", corium::K_STATIC_IP) ? "1" : "0",
             corium::Prefs::getString("net", corium::K_IP),
             corium::Prefs::getString("net", corium::K_GW),
             corium::Prefs::getString("net", corium::K_SN),
             corium::Prefs::getString("net", corium::K_DNS));
}

// false = one refused
static bool put_net(char** fields) {
  bool ok = corium::Prefs::put("net", corium::K_STATIC_IP, fields[0]);
  ok &= corium::Prefs::put("net", corium::K_IP,  fields[1]);
  ok &= corium::Prefs::put("net", corium::K_GW,  fields[2]);
  ok &= corium::Prefs::put("net", corium::K_SN,  fields[3]);
  ok &= corium::Prefs::put("net", corium::K_DNS, fields[4]);
  return ok;
}

bool corium::Wifi::saveNetBackup() {
  if (!corium::Fs::begin()) return false;
  // keep last good
  if (corium::Fs::exists(NET_BACKUP_PATH)) return false;
  char raw[NET_LINES_MAX];
  net_lines(raw, sizeof(raw));
  return corium::Fs::writeWhole(NET_BACKUP_PATH, raw, strlen(raw));
}

bool corium::Wifi::probeReachability() {
  IPAddress ip;
  const char* host = corium::Prefs::getString("ntp", corium::K_SERVER);
  // an ip skips dns, so no test
  if (!host || !*host || ip.fromString(host)) host = NTP_SERVER;
  return WiFi.hostByName(host, ip);
}

bool corium::Wifi::restoreNetBackup() {
  File f = corium::Fs::openR(NET_BACKUP_PATH);
  if (!f) return false;
  char raw[NET_LINES_MAX];
  char* fields[5];
  read_lines(f, raw, sizeof(raw), fields, 5);
  f.close();
  char cur[NET_LINES_MAX];
  net_lines(cur, sizeof(cur));
  if (!put_net(fields) || !corium::Prefs::commit()) {
    // back to what runs
    splitLines(cur, fields, 5);
    put_net(fields);
    return false;
  }
  corium::Fs::remove(NET_BACKUP_PATH);
  return true;
}

void corium::Wifi::dropNetBackup() { corium::Fs::remove(NET_BACKUP_PATH); }

bool corium::Wifi::validateStatic(const char* ip, const char* gw, const char* sn,
                          char* err, size_t cap) {
  IPAddress ipa, gwa, sna;
  if (!ipa.fromString(ip)) {
    if (err) snprintf_P(err, cap, PSTR("invalid IP address: %s"), ip);
    return false;
  }
  if (!gwa.fromString(gw)) {
    if (err) snprintf_P(err, cap, PSTR("invalid gateway: %s"), gw);
    return false;
  }
  if (!sna.fromString(sn)) {
    if (err) snprintf_P(err, cap, PSTR("invalid subnet mask: %s"), sn);
    return false;
  }
  // gw on the same subnet
  if (((uint32_t)ipa & (uint32_t)sna) != ((uint32_t)gwa & (uint32_t)sna)) {
    if (err) snprintf_P(err, cap,
      PSTR("gateway %s outside subnet %s/%s"), gw, ip, sn);
    return false;
  }
  return true;
}

bool corium::Wifi::applyStaticConfig() {
  if (!corium::Prefs::getBool("net", corium::K_STATIC_IP)) return false;
  const char* ip  = corium::Prefs::getString("net", corium::K_IP);
  const char* gw  = corium::Prefs::getString("net", corium::K_GW);
  const char* sn  = corium::Prefs::getString("net", corium::K_SN);
  const char* dns = corium::Prefs::getString("net", corium::K_DNS);
  char err[96];
  if (!corium::Wifi::validateStatic(ip, gw, sn, err, sizeof(err))) {
    corium::Log::console(PSTR("WiFi: Static IP rejected (%s); using DHCP"), err);
    return false;
  }
  IPAddress ipa, gwa, sna, dnsa;
  ipa.fromString(ip);
  gwa.fromString(gw);
  sna.fromString(sn);
  if (!dnsa.fromString(dns)) dnsa = gwa;
  WiFi.config(ipa, gwa, sna, dnsa);
  corium::Log::console(PSTR("WiFi: Static %u.%u.%u.%u gw=%u.%u.%u.%u sn=%u.%u.%u.%u dns=%u.%u.%u.%u"),
               ipa[0], ipa[1], ipa[2], ipa[3],
               gwa[0], gwa[1], gwa[2], gwa[3],
               sna[0], sna[1], sna[2], sna[3],
               dnsa[0], dnsa[1], dnsa[2], dnsa[3]);
  return true;
}

// too long is refused, a cut one would never join; open = "", not nullptr
bool corium::Wifi::holdCreds(const char* ssid, const char* pass) {
  if (!ssid || !pass) return false;
  size_t sl = strlen(ssid), pl = strlen(pass);
  if (sl > EGPORTAL_SSID_MAX || pl > EGPORTAL_PASS_MAX) return false;
  if (!s_creds) s_creds = (Creds*)malloc(sizeof(Creds));
  if (!s_creds) return false;
  memcpy(s_creds->ssid, ssid, sl + 1);
  memcpy(s_creds->pass, pass, pl + 1);
  return true;
}

const char* corium::Wifi::heldSsid() { return s_creds ? s_creds->ssid : ""; }

bool corium::Wifi::beginHeld() {
  if (!s_creds) return false;
  corium::Wifi::beginSaved(s_creds->ssid, s_creds->pass);
  corium::Wifi::dropHeld();
  return true;
}

void corium::Wifi::dropHeld() {
  free(s_creds);
  s_creds = nullptr;
}

// outside the portal Improv joins by itself
void corium::Wifi::onImprovCreds(const char* ssid, const char* pass) {
  if (s_in_portal && corium::Wifi::holdCreds(ssid, pass)) s_got_creds = true;
}

static void reset_wifi_tx_power_default();

// retries, slow AP after a power blip
static bool join_saved_network() {
  bool associated = false;
  for (uint8_t attempt = 0; attempt <= JOIN_RETRIES && !associated && !corium::Wifi::offlineForced; attempt++) {
    if (attempt > 0) corium::Log::console(PSTR("WiFi: Retrying saved network (%u/" STR(JOIN_RETRIES) ")"), attempt);
    WiFi.begin();                          // uses NVS-stored creds
    associated = wait_wifi(attempt == 0 ? JOIN_WAIT_MS : JOIN_RETRY_WAIT_MS);
  }
  return associated;
}

// associated but may not route
static void probe_new_static_config(bool static_on) {
  if (!static_on || !corium::Fs::exists(NET_BACKUP_PATH)) return;
  corium::Log::console(PSTR("WiFi: Probing reachability of new static config"));
  if (corium::Wifi::probeReachability()) {
    corium::Log::console(PSTR("WiFi: Static config probe OK"));
    return;
  }
  corium::Log::console(PSTR("WiFi: Static config probe failed, reverting"));
  if (corium::Wifi::restoreNetBackup()) {
    corium::DeviceInfo::safeRestart(500);
    return;
  }
  // a restart boots the same config, next boot tries again
  corium::Log::console(PSTR("WiFi: Revert not saved"));
  corium::Wifi::backupGraceDone = true;
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
      corium::DeviceInfo::hostname());
    portal->setNotice(notice);
  }
  portal->onSave([](const char* ssid, const char* pw, void*) {
    if (corium::Wifi::holdCreds(ssid, pw)) s_got_creds = true;
  });
  portal->onReset([](void*) { s_reset = true; });
  // open AP but every page asks, as the web UI
  portal->setBasicAuth(F("admin"), corium::Prefs::getString("sys", corium::K_WEB_PASS), F(THING_NAME));
  portal->begin(corium::DeviceInfo::hostname());
  reset_wifi_tx_power_default();   // after begin(), full power
  corium::Log::console(PSTR("WiFi: Setup portal up on AP %s"), corium::DeviceInfo::hostname());
  return notice;
}

static void portal_factory_reset(EGPortal* portal) {
  // let the page flush
  unsigned long flush_until = corium::fastMillis() + 1200;
  while (!corium::reached(corium::fastMillis(), flush_until)) {
    portal->loop();
    delay(10);
    yield();
  }
  corium::Log::console(PSTR("WiFi: Factory reset from setup portal"));
  corium::DeviceInfo::factoryReset();
  corium::DeviceInfo::safeRestart(200);
}

// hasSavedCreds() here drops the AP
static void run_portal(EGPortal* portal, bool have_creds) {
  unsigned long idle_ms = corium::fastMillis();
  while (!s_got_creds && !corium::Wifi::offlineForced) {
    portal->loop();
    console.pollBoot();
    delay(10);
    yield();
    if (s_reset) portal_factory_reset(portal);
    if (!have_creds) continue;   // first setup, wait forever
    if (WiFi.softAPgetStationNum() > 0) idle_ms = corium::fastMillis();
    if ((corium::fastMillis() - idle_ms) >= PORTAL_RETRY_MS ||
        corium::fastMillis() - CORIUM_MS_START >= WIFI_OFFLINE_MAX_MS) {
      corium::Log::console(PSTR("WiFi: Portal timeout, rebooting to retry saved network"));
      corium::DeviceInfo::safeRestart(500);
    }
  }
}

// true = a static IP set
static bool sta_up() {
  WiFi.mode(WIFI_STA);
  corium::Wifi::applyRuntimeNetPrefs();
  return corium::Wifi::applyStaticConfig();
}

static bool join_or_portal() {
  bool have_creds = corium::Wifi::hasSavedCreds();
  if (have_creds) {
    bool static_on = sta_up();
    if (join_saved_network()) {
      probe_new_static_config(static_on);
      return true;
    }
    if (corium::Wifi::offlineForced) return false;
    corium::Log::console(PSTR("WiFi: Saved creds failed to associate"));
    if (restore_backup()) return true;
    if (corium::Wifi::OFFLINE_OK) return false;
  }

  s_got_creds = false;
  auto* portal = new EGPortal();
  char* notice = setup_portal(portal);
  s_in_portal = true;
  run_portal(portal, have_creds);
  s_in_portal = false;
  portal->end();
  delete portal;
  free(notice);
  if (corium::Wifi::offlineForced) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  delay(500);

  sta_up();
  if (!corium::Wifi::beginHeld()) return false;
  return wait_wifi(JOIN_WAIT_MS);
}

bool corium::Wifi::offlineNow() {
  if (!s_joining) return false;
  corium::Wifi::offlineForced = true;
  return true;
}

// own AP, the whole UI, no network
static void start_ap() {
  WiFi.mode(WIFI_AP);
  corium::Wifi::applyRuntimeNetPrefs();
  const char* pass = corium::Prefs::getString("sys", corium::K_WEB_PASS);
  // short or none: open, the pages still ask
  bool locked = pass && strlen(pass) >= AP_PASS_MIN;
  bool hide = corium::Prefs::getBool("wifi", K_AP_HIDE);
  if (!WiFi.softAP(corium::DeviceInfo::hostname(), locked ? pass : nullptr, 1, hide)) {
    corium::Log::console(PSTR("WiFi: AP failed"));
    return;
  }
  corium::Wifi::ap = true;
  corium::Wifi::local_ip = WiFi.softAPIP();   // what the pages and screen show
  char ip[16];
  corium::Wifi::formatIP(ip, sizeof(ip));
  corium::Log::console(PSTR("WiFi: AP %s at %s (%s)"), corium::DeviceInfo::hostname(), ip,
               locked ? PSTR("web password") : PSTR("open"));
}

// a network to join, not the AP; saved with the next commit
void corium::Wifi::joinMode() { corium::Prefs::put("wifi", K_MODE, "0"); }

bool corium::Wifi::portalAhead() {
  if (corium::Wifi::offlineForced || corium::Prefs::getUInt("wifi", K_MODE) != 0) return false;
  return !corium::Wifi::hasSavedCreds();
}

bool corium::Wifi::connectOrPortal() {
  uint32_t mode = corium::Prefs::getUInt("wifi", K_MODE);
  if (mode == MODE_OFF || mode == MODE_AP) {
    corium::Wifi::offlineForced = true;
    if (mode == MODE_AP) start_ap();
    else corium::Log::console(PSTR("WiFi: off (wifi.mode)"));
    return false;
  }
  s_joining = true;
  bool up = join_or_portal();
  s_joining = false;
  return up;
}

static_assert(corium::Wifi::TOPIC_CHANGED < CORIUM_TOPIC_APP, "Corium topics sit below CORIUM_TOPIC_APP");

// the one writer of Wifi::*
class WifiModule : public corium::Module {
public:
  const char* name() override { return PSTR("wifi"); }
  bool coreModule() override { return true; }
  uint8_t displayOrder() override { return 0; }
  const corium::PrefGroup* prefsGroup() override;
  void onPrefsLoaded() override;
  void begin() override;
  void loop(unsigned long now) override;
};

static WifiModule wifimodule;
CORIUM_REGISTER_MODULE(wifimodule)

// SDK events cost the 8266 ~1.2 KB
void WifiModule::begin() {
  corium::Registry::every(this, 1000);
}

void WifiModule::loop(unsigned long now) {
  if (corium::Wifi::look(now)) corium::Registry::emit(corium::Wifi::TOPIC_CHANGED);
}

#define WIFI_SLEEP_DEFAULT "1"

static const corium::Pref WIFI_PREF_ITEMS[] PROGMEM = {
  {K_SLEEP,    nullptr, nullptr, WIFI_SLEEP_DEFAULT, nullptr, 0, 2,  0, corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL},
  {K_TX_POWER, nullptr, nullptr, "0",                nullptr, 0, 20, 0, corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL},
  {K_COUNTRY,  nullptr, nullptr, "",                 nullptr, 0, 0,  3, corium::P_STRING, corium::P_HIDDEN | corium::P_LOCAL},
  {K_PHY_MODE, nullptr, nullptr, "0",                nullptr, 0, 3,  0, corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL},
  {K_MODE,     nullptr, nullptr, "0",                nullptr, 0, 2,  0, corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL | corium::P_REBOOT},
  {K_AP_HIDE,  nullptr, nullptr, "0",                nullptr, 0, 0,  0, corium::P_BOOL,   corium::P_HIDDEN | corium::P_LOCAL | corium::P_REBOOT},
};

static const corium::PrefGroup WIFI_PREF_GROUP PROGMEM = {
  "wifi", "Wi-Fi", 1,
  WIFI_PREF_ITEMS,
  sizeof(WIFI_PREF_ITEMS) / sizeof(WIFI_PREF_ITEMS[0]),
};

const corium::PrefGroup* WifiModule::prefsGroup() { return &WIFI_PREF_GROUP; }

static int8_t s_rx_sleep = -1;   // the receiver's, wins over the pref

static void apply_wifi_sleep() {
  // always, SDK keeps it in NVS
  bool rx = s_rx_sleep >= 0;
  uint8_t mode = rx ? (uint8_t)s_rx_sleep : (uint8_t)corium::Prefs::getUInt("wifi", K_SLEEP);
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
    corium::Log::console(PSTR("WiFi: tx_power %u too low, using %u dBm"), dBm, WIFI_TX_POWER_MIN_DBM);
    dBm = WIFI_TX_POWER_MIN_DBM;
  }
#ifdef ESP8266
  WiFi.setOutputPower((float)dBm);
#else
  esp_wifi_set_max_tx_power((int8_t)(dBm * 4));   // quarter dBm
#endif
}

static void reset_wifi_tx_power_default() {
#ifdef ESP8266
  WiFi.setOutputPower(20.5f);
#else
  esp_wifi_set_max_tx_power(80);   // 20 dBm
#endif
}

#ifdef ESP8266
// esp32 knows its own
static uint8_t country_chans(const char* code) {
  if (!strncasecmp_P(code, PSTR("US"), 2) || !strncasecmp_P(code, PSTR("CA"), 2)) return 11;
  if (!strncasecmp_P(code, PSTR("JP"), 2)) return 14;
  return 13;
}
#endif

static void apply_wifi_country(const char* code) {
  if (!code || !code[0]) return;
#ifdef ESP8266
  wifi_country_t c = {0};
  memcpy(c.cc, code, strnlen(code, sizeof(c.cc)));   // "USA" fills it, no NUL
  c.schan  = 1;
  c.nchan  = country_chans(code);
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

void WifiModule::onPrefsLoaded() {
  // rest needs WiFi.mode() first
  apply_wifi_sleep();
}

void corium::Wifi::rxSleep(int8_t mode) {
  s_rx_sleep = mode;
  apply_wifi_sleep();
}

void corium::Wifi::reconnect() { WiFi.reconnect(); }

void corium::Wifi::applyRuntimeNetPrefs() {
  apply_wifi_tx_power((uint8_t)corium::Prefs::getUInt  ("wifi", K_TX_POWER));
  apply_wifi_country (         corium::Prefs::getString("wifi", K_COUNTRY));
  apply_wifi_phy_mode((uint8_t)corium::Prefs::getUInt  ("wifi", K_PHY_MODE));
}

int corium::Wifi::txPowerQdbm() {
#ifdef ESP8266
  uint8_t dBm = (uint8_t)corium::Prefs::getUInt("wifi", K_TX_POWER);
  if (dBm == 0) return 82;   // core default 20.5 dBm
  if (dBm < WIFI_TX_POWER_MIN_DBM) dBm = WIFI_TX_POWER_MIN_DBM;
  return (int)dBm * 4;
#else
  int8_t q = 0;
  if (esp_wifi_get_max_tx_power(&q) != ESP_OK) return -1;
  return (int)q;
#endif
}

const char* corium::Wifi::phyModeStr() {
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

const char* corium::Wifi::sleepModeStr() {
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
