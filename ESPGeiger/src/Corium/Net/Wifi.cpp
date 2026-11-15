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
#include "Corium/System/BootGuard.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Fs.h"
#include "Corium/System/Lifecycle.h"
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
static bool s_reset = false;

// the join test, one at a time
static uint8_t s_join = EGPortal::JOIN_IDLE;
static uint8_t s_fail = EGPortal::JOIN_IDLE;   // what BACK ends with
static corium::Deadline s_join_due;
static uint32_t s_join_ms = 0;
static bool s_left = false;   // down since begin(), the status lags it; an up only counts after
static Creds* s_back = nullptr;   // the network left, test only
static char s_test_ssid[WIFI_SSID_MAX + 1];
static const uint32_t PORTAL_DONE_MS = 3000;   // the page polls each 2 s, it sees OK
static const uint32_t TEST_STEP_MS = 200;
static corium::Deadline s_rebegin;   // no sdk retries, one more begin() half way
#ifdef ESP32
static volatile uint8_t s_reason = 0;   // the event task's, the last disconnect
static bool s_hooked = false;
#endif

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
static const uint32_t OUTAGE_SCAN_MS = 60000UL;
using corium::Wifi::JOIN_WAIT_MS;
static const uint32_t JOIN_RETRY_WAIT_MS = 15000;
static const uint32_t LEAVE_WAIT_MS = 2000;
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
  }
  return false;
}


bool corium::Wifi::clearSavedCreds() {
#ifdef ESP8266
  WiFi.persistent(true);
  (void)WiFi.disconnect(true);    // also clears creds, any mode; read back below
  WiFi.persistent(false);
  struct station_config c;
  bool ok = wifi_station_get_config_default(&c) && !c.ssid[0];
#else
  // disconnect(true, true) erases only with STA up, this in any mode
  bool ok = WiFi.eraseAP();
#endif
  // the backup holds them too
  ok &= corium::Fs::remove(WIFI_BACKUP_PATH);
  ok &= corium::Fs::remove(NET_BACKUP_PATH);
  corium::Log::console(PSTR("WiFi: SDK creds %scleared"), ok ? PSTR("") : PSTR("NOT "));
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

bool corium::Wifi::hasSavedCreds() {
  WiFi.mode(WIFI_STA);
  Creds c;
  return sdk_creds(c);
}

CORIUM_NODISCARD static bool write_backup(const Creds& c) {
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

static void begin_ram(const char* ssid, const char* pass);
CORIUM_NODISCARD static bool save_creds();

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
    (void)corium::Fs::remove(WIFI_BACKUP_PATH);   // stays = the same false next boot
    return false;
  }
  corium::Log::console(PSTR("WiFi: Restoring backup creds (ssid=%s)"), ssid);
  // joined first, flash after: a dead backup never replaces the saved creds
  begin_ram(ssid, pass);
  if (!wait_wifi(JOIN_WAIT_MS)) return false;
  if (save_creds()) {
    (void)corium::Fs::remove(WIFI_BACKUP_PATH);   // the 5 min stable clear tries again
  } else {
    // kept, the next boot tries again
    corium::Log::console(PSTR("WiFi: Backup creds NOT saved"));
    corium::Wifi::backupGraceDone = true;
  }
  corium::Log::console(PSTR("WiFi: Reverted to backup creds"));
  return true;
}

// 5 IPs + newlines
#define NET_LINES_MAX 96

// the backup file's lines and putNet's fields, in this order
static const char* const NET_KEYS[] PROGMEM = {
  corium::K_STATIC_IP, corium::K_IP, corium::K_GW, corium::K_SN, corium::K_DNS,
};
static const uint8_t NET_N = sizeof(NET_KEYS) / sizeof(NET_KEYS[0]);

static const char* net_key(uint8_t i) { return (const char*)pgm_read_ptr(&NET_KEYS[i]); }

static void net_lines(char* buf, size_t len) {
  buf[0] = '\0';
  for (uint8_t i = 0; i < NET_N; i++) {
    strlcat(buf, corium::Prefs::getString("net", net_key(i)), len);
    strlcat(buf, "\n", len);
  }
}

bool corium::Wifi::putNet(const char* const* fields) {
  bool ok = true;
  for (uint8_t i = 0; i < NET_N; i++) ok &= corium::Prefs::put("net", net_key(i), fields[i]);
  return ok;
}

bool corium::Wifi::saveNetBackup() {
  if (!corium::Fs::begin()) return false;
  // keep last good
  if (corium::Fs::exists(NET_BACKUP_PATH)) return true;
  char raw[NET_LINES_MAX];
  net_lines(raw, sizeof(raw));
  return corium::Fs::writeWhole(NET_BACKUP_PATH, raw, strlen(raw));
}

bool corium::Wifi::probeReachability() {
  IPAddress ip;
  const char* host = corium::Prefs::getString("ntp", corium::K_SERVER);
  // an ip skips dns, so no test
  if (!*host || ip.fromString(host)) host = NTP_SERVER;
  return WiFi.hostByName(host, ip);
}

bool corium::Wifi::restoreNetBackup() {
  File f = corium::Fs::openR(NET_BACKUP_PATH);
  if (!f) return false;
  char raw[NET_LINES_MAX];
  char* fields[NET_N];
  read_lines(f, raw, sizeof(raw), fields, NET_N);
  f.close();
  char cur[NET_LINES_MAX];
  net_lines(cur, sizeof(cur));
  if (!corium::Wifi::putNet(fields) || !corium::Prefs::commit()) {
    // back to what runs
    splitLines(cur, fields, NET_N);
    (void)corium::Wifi::putNet(fields);   // the running values, they fit
    return false;
  }
  (void)corium::Fs::remove(NET_BACKUP_PATH);   // the 5 min stable clear tries again
  return true;
}

// stays = the 5 min stable clear tries again
void corium::Wifi::dropNetBackup() { (void)corium::Fs::remove(NET_BACKUP_PATH); }

// ip, gw, sn into a[3]
static bool parse_static(const char* ip, const char* gw, const char* sn, IPAddress* a,
                         char* err, size_t cap) {
  if (!a[0].fromString(ip)) {
    if (err) snprintf_P(err, cap, PSTR("invalid IP address: %s"), ip);
    return false;
  }
  if (!a[1].fromString(gw)) {
    if (err) snprintf_P(err, cap, PSTR("invalid gateway: %s"), gw);
    return false;
  }
  if (!a[2].fromString(sn)) {
    if (err) snprintf_P(err, cap, PSTR("invalid subnet mask: %s"), sn);
    return false;
  }
  // gw on the same subnet
  if (((uint32_t)a[0] & (uint32_t)a[2]) != ((uint32_t)a[1] & (uint32_t)a[2])) {
    if (err) snprintf_P(err, cap,
      PSTR("gateway %s outside subnet %s/%s"), gw, ip, sn);
    return false;
  }
  return true;
}

bool corium::Wifi::validateStatic(const char* ip, const char* gw, const char* sn,
                          char* err, size_t cap) {
  IPAddress a[3];
  return parse_static(ip, gw, sn, a, err, cap);
}

bool corium::Wifi::applyStaticConfig() {
  if (!corium::Prefs::getBool("net", corium::K_STATIC_IP)) return false;
  const char* ip  = corium::Prefs::getString("net", corium::K_IP);
  const char* gw  = corium::Prefs::getString("net", corium::K_GW);
  const char* sn  = corium::Prefs::getString("net", corium::K_SN);
  const char* dns = corium::Prefs::getString("net", corium::K_DNS);
  char err[96];
  const char* why = err;
  IPAddress a[3];
  if (parse_static(ip, gw, sn, a, err, sizeof(err))) {
    const IPAddress& ipa = a[0];
    const IPAddress& gwa = a[1];
    const IPAddress& sna = a[2];
    IPAddress dnsa;
    if (!dnsa.fromString(dns)) dnsa = gwa;
    // both cores keep it for the boot, begin() then skips dhcp
    if (WiFi.config(ipa, gwa, sna, dnsa)) {
      corium::Log::console(PSTR("WiFi: Static %u.%u.%u.%u gw=%u.%u.%u.%u sn=%u.%u.%u.%u dns=%u.%u.%u.%u"),
                   ipa[0], ipa[1], ipa[2], ipa[3],
                   gwa[0], gwa[1], gwa[2], gwa[3],
                   sna[0], sna[1], sna[2], sna[3],
                   dnsa[0], dnsa[1], dnsa[2], dnsa[3]);
      return true;
    }
    why = PSTR("not applied");
  }
  corium::Log::console(PSTR("WiFi: Static IP rejected (%s); using DHCP"), why);
  return false;
}

// too long is refused, a cut one would never join; open = "", not nullptr
corium::Wifi::Hold corium::Wifi::holdCreds(const char* ssid, const char* pass) {
  if (!ssid || !pass) return HOLD_BAD;
  // one at a time: a test's, or /wifisave's till its restart
  if (s_creds) return HOLD_BUSY;
  size_t sl = strlen(ssid), pl = strlen(pass);
  if (sl > EGPORTAL_SSID_MAX || pl > EGPORTAL_PASS_MAX) return HOLD_BAD;
  s_creds = (Creds*)malloc(sizeof(Creds));
  if (!s_creds) return HOLD_NO_MEMORY;
  memcpy(s_creds->ssid, ssid, sl + 1);
  memcpy(s_creds->pass, pass, pl + 1);
  return HOLD_OK;
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

// Join test: RAM creds, flash only once joined

// RAM only, flash keeps the saved network
static void begin_ram(const char* ssid, const char* pass) {
#ifdef ESP32
  WiFi.enableSTA(true);   // set_storage needs the radio up, wifi.mode 1 never started it
  esp_wifi_set_storage(WIFI_STORAGE_RAM);   // persistent() is read once, at the first mode()
#else
  WiFi.persistent(false);
#endif
  (void)WiFi.begin(ssid, pass);   // the step reads the status
}

// the running config to flash; begin() would connect again or write nothing
CORIUM_NODISCARD static bool save_creds() {
#ifdef ESP32
  wifi_config_t c = {};
  if (esp_wifi_set_storage(WIFI_STORAGE_FLASH) != ESP_OK) return false;
  if (esp_wifi_get_config(WIFI_IF_STA, &c) != ESP_OK) return false;
  return esp_wifi_set_config(WIFI_IF_STA, &c) == ESP_OK;
#else
  struct station_config c;
  return wifi_station_get_config(&c) && wifi_station_set_config(&c);
#endif
}

#ifdef ESP32
// event task, one byte; a leave (begin()'s, the core's retry) never hides a real reason
static void on_drop(arduino_event_t* e) {
  const wifi_event_sta_disconnected_t& d = e->event_info.wifi_sta_disconnected;
  // the network before's, queued ahead of the test
  if (d.ssid_len && (d.ssid_len != strlen(s_test_ssid) || memcmp(d.ssid, s_test_ssid, d.ssid_len))) return;
  if (d.reason != WIFI_REASON_ASSOC_LEAVE || !s_reason) s_reason = d.reason;
}

// status alone can't name a wrong password
static uint8_t why(uint8_t reason) {
  switch (reason) {
    case 0:
    case WIFI_REASON_ASSOC_LEAVE:   return EGPortal::JOIN_NO_REPLY;   // begin()'s own leave
    case WIFI_REASON_NO_AP_FOUND:   return EGPortal::JOIN_NO_SSID;
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED: return EGPortal::JOIN_BAD_PASS;
    default:                        return EGPortal::JOIN_REFUSED;
  }
}

// a stale status lingers till the next event, so the reason since begin() decides
static uint8_t join_result(uint32_t now) {
  uint8_t reason = s_reason;   // before the status, the core sets that first
  wl_status_t st = WiFi.status();
  bool up = st == WL_CONNECTED;
  if (reason || !up) s_left = true;
  if (up && s_left) return EGPortal::JOIN_OK;
  uint8_t r = why(reason);
  if (r == EGPortal::JOIN_NO_SSID) return r;
  if (st == WL_CONNECT_FAILED && r != EGPortal::JOIN_NO_REPLY) return r;   // no retry left
  return s_join_due.due(now) ? r : EGPortal::JOIN_TESTING;
}
#else
static uint8_t join_result(uint32_t now) {
  wl_status_t st = WiFi.status();
  if (st != WL_CONNECTED) s_left = true;
  switch (st) {
    case WL_CONNECTED:      if (s_left) return EGPortal::JOIN_OK; break;
    case WL_NO_SSID_AVAIL:  return EGPortal::JOIN_NO_SSID;
    case WL_WRONG_PASSWORD: return EGPortal::JOIN_BAD_PASS;
    case WL_CONNECT_FAILED: return EGPortal::JOIN_REFUSED;
    default: break;
  }
  return s_join_due.due(now) ? EGPortal::JOIN_NO_REPLY : EGPortal::JOIN_TESTING;
}
#endif

static void end_test() {
  corium::Wifi::dropHeld();
  free(s_back);
  s_back = nullptr;
  s_join_due.clear();
  WiFi.setAutoReconnect(true);   // esp32 store stays RAM, only a save writes NVS
}

static PGM_P fail_text(uint8_t r) {
  switch (r) {
    case EGPortal::JOIN_NO_SSID:  return PSTR("network not found");
    case EGPortal::JOIN_BAD_PASS: return PSTR("password refused");
    case EGPortal::JOIN_REFUSED:  return PSTR("refused");
    default:                      return nullptr;
  }
}

static void test_failed(uint8_t r, uint32_t now) {
  PGM_P t = fail_text(r);
  if (t) corium::Log::console(PSTR("WiFi: Join test: %s"), t);
  else corium::Log::console(PSTR("WiFi: Join test: no reply in %u s"), (unsigned)(s_join_ms / 1000));
  // no erase; AP mode drops the STA again
  (void)WiFi.disconnect(corium::Wifi::ap, false);
  if (!s_back) {
    s_join = r;
    end_test();
    return;
  }
  begin_ram(s_back->ssid, s_back->pass);
  s_fail = r;
  s_join = EGPortal::JOIN_BACK;
  s_join_due.set(now + JOIN_WAIT_MS);
}

static void test_joined() {
  bool saved = save_creds();
  corium::Log::console(PSTR("WiFi: Join test: joined, creds %ssaved"), saved ? PSTR("") : PSTR("NOT "));
  if (saved) (void)corium::Fs::remove(WIFI_BACKUP_PATH);   // tested beats a revert
  s_join = EGPortal::JOIN_OK;
  end_test();
}

// a test cut short, the radio as it is
static void stop_test() {
  if (!corium::Wifi::testing()) return;
  s_join = EGPortal::JOIN_IDLE;
  end_test();
}

bool corium::Wifi::testJoin(uint32_t ms) {
  if (!s_creds) return false;
  // normal run: the saved network to go back to; portal and AP mode have none
  if (!s_in_portal && !corium::Wifi::ap) {
    s_back = (Creds*)malloc(sizeof(Creds));
    if (!s_back) {
      corium::Wifi::dropHeld();   // else the next hold is refused
      return false;
    }
    if (!sdk_creds(*s_back)) {
      free(s_back);
      s_back = nullptr;
    }
  }
  strlcpy(s_test_ssid, s_creds->ssid, sizeof(s_test_ssid));
  corium::Log::console(PSTR("WiFi: Join test '%s'"), s_test_ssid);
  s_left = !corium::Wifi::connectedRaw();
  // on it already: begin() would change nothing and say nothing (esp32 returns, no event)
  if (!s_left && s_back && !strcmp(s_back->ssid, s_creds->ssid) && !strcmp(s_back->pass, s_creds->pass)) {
    test_joined();
    return true;
  }
  WiFi.setAutoReconnect(false);
#ifdef ESP32
  if (!s_hooked) WiFi.onEvent(on_drop, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);   // once, a byte outside tests
  s_hooked = true;
  s_reason = 0;
#endif
  begin_ram(s_creds->ssid, s_creds->pass);
  uint32_t now = corium::fastMillis();
  s_join_ms = ms;
  s_rebegin.set(now + ms / 2);
  s_join_due.set(now + ms);
  s_join = EGPortal::JOIN_TESTING;
  return true;
}

bool corium::Wifi::testStep(uint32_t now) {
  if (s_join == EGPortal::JOIN_TESTING) {
    uint8_t r = join_result(now);
    if (r == EGPortal::JOIN_TESTING) {
      if (s_rebegin.due(now)) {
        s_rebegin.clear();
        begin_ram(s_creds->ssid, s_creds->pass);   // its leave never hides the reason (on_drop)
      }
      return true;
    }
    if (r == EGPortal::JOIN_OK) test_joined();
    else test_failed(r, now);
  } else if (s_join == EGPortal::JOIN_BACK) {
    bool up = corium::Wifi::connectedRaw();
    if (!up && !s_join_due.due(now)) return true;
    corium::Log::console(PSTR("WiFi: Join test: back on '%s'%s"), s_back->ssid, up ? PSTR("") : PSTR(" failed"));
    s_join = s_fail;
    end_test();
  }
  return corium::Wifi::testing();
}

uint8_t corium::Wifi::testState() { return s_join; }
bool corium::Wifi::testing() { return s_join == EGPortal::JOIN_TESTING || s_join == EGPortal::JOIN_BACK; }
const char* corium::Wifi::testSsid() { return s_test_ssid; }

static void reset_wifi_tx_power_default();
static void reset_wifi_country();
static void apply_wifi_phy_mode(uint8_t mode);

// retries, slow AP after a power blip
CORIUM_NODISCARD static bool join_saved_network() {
  bool associated = false;
  for (uint8_t attempt = 0; attempt <= JOIN_RETRIES && !associated && !corium::Wifi::offlineForced; attempt++) {
    if (attempt > 0) corium::Log::console(PSTR("WiFi: Retrying saved network (%u/" STR(JOIN_RETRIES) ")"), attempt);
    (void)WiFi.begin();                    // NVS creds; wait_wifi reads the status
    associated = wait_wifi(attempt == 0 ? JOIN_WAIT_MS : JOIN_RETRY_WAIT_MS);
  }
  return associated;
}

// failed with radio rows set: once more at the chip's own, this boot only
CORIUM_NODISCARD static bool join_at_defaults() {
  bool tx  = corium::Prefs::getUInt("wifi", K_TX_POWER) != 0;
  bool cc  = corium::Prefs::getString("wifi", K_COUNTRY)[0] != '\0';
  bool phy = corium::Prefs::getUInt("wifi", K_PHY_MODE) != 0;
  if (!tx && !cc && !phy) return false;
  corium::Log::console(PSTR("WiFi: Retrying without wifi%s%s%s"), tx ? PSTR(" tx_power") : PSTR(""),
                       cc ? PSTR(" country") : PSTR(""), phy ? PSTR(" phy_mode") : PSTR(""));
  // all three, the unset ones are there already
  reset_wifi_tx_power_default();
  reset_wifi_country();
  apply_wifi_phy_mode(3);   // b/g/n, the chip's own
  (void)WiFi.begin();
  return wait_wifi(JOIN_WAIT_MS);
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
    return corium::Wifi::holdCreds(ssid, pw) == corium::Wifi::HOLD_OK && corium::Wifi::testJoin();
  });
  portal->onReset([](void*) { s_reset = true; });
  // open AP but every page asks, as the web UI
  portal->setBasicAuth(F("admin"), corium::Prefs::getString("sys", corium::K_WEB_PASS), F(THING_NAME));
  portal->setAuthHelp(corium::AUTH_HELP);
  bool up = portal->begin(corium::DeviceInfo::hostname());
  reset_wifi_tx_power_default();   // after begin(), full power
  corium::Log::console(PSTR("WiFi: Setup portal %sup on AP %s"), up ? PSTR("") : PSTR("NOT "),
                       corium::DeviceInfo::hostname());
  return notice;
}

static void portal_factory_reset(EGPortal* portal) {
  // let the page flush
  unsigned long flush_until = corium::fastMillis() + 1200;
  while (!corium::reached(corium::fastMillis(), flush_until)) {
    portal->loop();
    delay(portal->busy() ? 1 : 10);
  }
  corium::Log::console(PSTR("WiFi: Factory reset from setup portal"));
  corium::DeviceInfo::factoryReset();
  corium::DeviceInfo::safeRestart(200);
}

// saved creds only; never mid test, the next pass after it
static bool portal_timed_out(uint32_t& idle_ms, uint32_t now) {
  if (WiFi.softAPgetStationNum() > 0) idle_ms = now;
  if (corium::Wifi::testing()) return false;
  return now - idle_ms >= PORTAL_RETRY_MS || now - CORIUM_MS_START >= WIFI_OFFLINE_MAX_MS;
}

// the saved ap not heard as the portal came up (an outage): back = rejoin
static bool ap_back(EGPortal* portal, const char* ssid, corium::Deadline& scan, uint32_t now) {
  if (WiFi.softAPgetStationNum() > 0 || corium::Wifi::testing()) {
    scan.set(now + OUTAGE_SCAN_MS);   // a scan hops channel, a phone on it drops
    return false;
  }
  if (portal->heard(ssid)) return true;
  if (scan.due(now)) {
    portal->kickScan();
    scan.set(now + OUTAGE_SCAN_MS);
  }
  return false;
}

// hasSavedCreds() here drops the AP
static void run_portal(EGPortal* portal, bool have_creds) {
  uint32_t idle_ms = corium::fastMillis();
  corium::Deadline done;   // joined, the page shows it first
  Creds c;
  bool watch = have_creds && sdk_creds(c) && !portal->heard(c.ssid);
  corium::Deadline scan;
  scan.set(idle_ms + OUTAGE_SCAN_MS);
  while (!done.due(corium::fastMillis()) && !corium::Wifi::offlineForced) {
    portal->loop();
    console.pollBoot();
    delay(portal->busy() ? 1 : 10);   // work waiting, back in 1 ms
    uint32_t now = corium::fastMillis();
    corium::Wifi::testStep(now);
    portal->setJoinState(s_join);
    if (s_join == EGPortal::JOIN_OK && !done.armed()) done.set(now + PORTAL_DONE_MS);
    if (s_reset) portal_factory_reset(portal);
    if (!have_creds) continue;   // first setup, wait forever
    if (watch && ap_back(portal, c.ssid, scan, now)) {
      corium::Log::console(PSTR("WiFi: %s back, restarting to join"), c.ssid);
      corium::DeviceInfo::safeRestart(500);
    }
    if (portal_timed_out(idle_ms, now)) {
      corium::Log::console(PSTR("WiFi: Portal timeout, restarting to retry saved network"));
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

CORIUM_NODISCARD static bool join_or_portal() {
  bool have_creds = corium::Wifi::hasSavedCreds();
  if (have_creds) {
    bool static_on = sta_up();
    if (join_saved_network() || (!corium::Wifi::offlineForced && join_at_defaults())) {
      probe_new_static_config(static_on);
      return true;
    }
    if (corium::Wifi::offlineForced) return false;
    Creds c;
    corium::Log::console(PSTR("WiFi: Saved creds failed to associate (ssid=%s)"), sdk_creds(c) ? c.ssid : "");
    if (restore_backup()) return true;
    if (corium::Wifi::OFFLINE_OK && !corium::BootGuard::portalAsked()) return false;   // a save's boot gets one
  }

  auto* portal = new EGPortal();
  char* notice = setup_portal(portal);
  s_in_portal = true;
  run_portal(portal, have_creds);
  s_in_portal = false;
  portal->end();
  delete portal;
  free(notice);
  if (corium::Wifi::offlineForced) {
    stop_test();
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // the AP off can drop it; a static set on the test's join never answers arp (8266), join again
  bool static_on = sta_up();
  if (!corium::Wifi::connectedRaw()) (void)WiFi.begin();
  else if (static_on) {
    (void)WiFi.reconnect();
    // the old link reads up till it drops (esp32)
    uint32_t start = corium::fastMillis();
    while (WiFi.status() == WL_CONNECTED && corium::fastMillis() - start < LEAVE_WAIT_MS) {
      console.pollBoot();
      delay(10);
    }
  }
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
  bool locked = strlen(pass) >= AP_PASS_MIN;
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
bool corium::Wifi::joinMode() { return corium::Prefs::put("wifi", K_MODE, "0"); }

// joined from hotspot or off mode: the next boot joins it too, a restart onto it
bool corium::Wifi::keepJoined(uint32_t restart_ms) {
  if (corium::Prefs::getUInt("wifi", K_MODE) == 0) return false;
  if (!joinMode() || !corium::Prefs::commit()) {
    corium::Log::console(PSTR("WiFi: Network settings not saved"));
    return false;
  }
  corium::Log::console(PSTR("WiFi: Moved to '%s', restarting"), s_test_ssid);
  corium::Lifecycle::requestRestart(corium::fastMillis(), restart_ms);
  return true;
}

bool corium::Wifi::portalAhead() {
  if (corium::Wifi::offlineForced || corium::Prefs::getUInt("wifi", K_MODE) != 0) return false;
  return !corium::Wifi::hasSavedCreds();
}

static void apply_strongest_ap();

bool corium::Wifi::connectOrPortal() {
  apply_strongest_ap();   // before any begin(), a portal save is built with it
  uint32_t mode = corium::Prefs::getUInt("wifi", K_MODE);
  if (mode == MODE_OFF || mode == MODE_AP) {
    corium::Wifi::offlineForced = true;
    if (mode == MODE_AP) start_ap();
    else corium::Log::console(PSTR("WiFi: Off (wifi.mode)"));
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
  if (corium::Wifi::testing() && corium::Wifi::testStep(now)) corium::Registry::dueIn(this, TEST_STEP_MS);
  if (corium::Wifi::look(now)) corium::Registry::emit(corium::Wifi::TOPIC_CHANGED);
}

#define WIFI_SLEEP_DEFAULT "1"

static const corium::Pref WIFI_PREF_ITEMS[] PROGMEM = {
  {K_SLEEP,    nullptr, nullptr, WIFI_SLEEP_DEFAULT, nullptr, 0, 2,  0, corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL},
  {K_TX_POWER, nullptr, nullptr, "0",                nullptr, 0, 20, 0, corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL | corium::P_REBOOT},
  {K_COUNTRY,  nullptr, nullptr, "",                 nullptr, 0, 0,  3, corium::P_STRING, corium::P_HIDDEN | corium::P_LOCAL | corium::P_REBOOT},
  {K_PHY_MODE, nullptr, nullptr, "0",                nullptr, 0, 3,  0, corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL | corium::P_REBOOT},
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

void corium::Wifi::applySleep() {
  // always, SDK keeps it in NVS
  bool rx = s_rx_sleep >= 0;
  uint8_t mode = rx ? (uint8_t)s_rx_sleep : (uint8_t)corium::Prefs::getUInt("wifi", K_SLEEP);
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

// the sdk's own, both chips: CN 1-13, the AP's once joined
static void reset_wifi_country() {
#ifdef ESP8266
  wifi_country_t c = {0};
  memcpy_P(c.cc, PSTR("CN"), 2);
  c.schan  = 1;
  c.nchan  = 13;
  c.policy = WIFI_COUNTRY_POLICY_AUTO;
  wifi_set_country(&c);
#else
  esp_wifi_set_country_code("CN", true);
#endif
}

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

// one ssid on many aps: esp32 joins the first it hears unless it scans every channel.
// begin(ssid, pass) takes the setters, begin() the stored config
static void apply_strongest_ap() {
#ifdef ESP32
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  wifi_config_t c;
  if (esp_wifi_get_config(WIFI_IF_STA, &c) != ESP_OK) return;
  if (c.sta.scan_method == WIFI_ALL_CHANNEL_SCAN && c.sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL) return;
  c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  (void)esp_wifi_set_config(WIFI_IF_STA, &c);
#endif
}

void WifiModule::onPrefsLoaded() {
  // rest needs WiFi.mode() first
  corium::Wifi::applySleep();
}

void corium::Wifi::rxSleep(int8_t mode) {
  s_rx_sleep = mode;
  corium::Wifi::applySleep();
}

void corium::Wifi::reconnect() { WiFi.reconnect(); }

void corium::Wifi::applyRuntimeNetPrefs() {
  apply_wifi_tx_power((uint8_t)corium::Prefs::getUInt  ("wifi", K_TX_POWER));
  apply_wifi_country (         corium::Prefs::getString("wifi", K_COUNTRY));
  apply_wifi_phy_mode((uint8_t)corium::Prefs::getUInt  ("wifi", K_PHY_MODE));
  apply_strongest_ap();
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
