/*
  SystemPrefs.cpp - The device's own prefs: system and network.

  Copyright (C) 2025 @steadramon

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
#include "SystemPrefs.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Fs.h"
#include "Corium/System/KeyStore.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/WebPortal/WebPortal.h"

CORIUM_PSTR(K_NAME,           "name");
CORIUM_PSTR(K_CPU_MHZ,        "cpu_mhz");

// System prefs

corium::SystemPrefs sysprefs;
CORIUM_REGISTER_MODULE(sysprefs)

CORIUM_PSTR(SY_L_NAM, "Friendly name");
CORIUM_PSTR(SY_H_NAM, "Optional label for the web pages");
CORIUM_PSTR(SY_H_WPW, "User admin, 8+ chars, also the hotspot's WiFi password, blank = no login");
CORIUM_PSTR(SY_L_OTA, "Arduino OTA");
CORIUM_PSTR(SY_H_OTA, "For PlatformIO (espota) and the Arduino IDE network port. Not needed for the Update page");
#ifdef ESP32
CORIUM_PSTR(SY_O_MHZ, "0|80|160|240");
#endif

static const corium::Pref SYSTEM_PREF_ITEMS[] PROGMEM = {
  {K_NAME,        SY_L_NAM, SY_H_NAM, "",      nullptr, 0, 0,    CORIUM_NAME_MAX, corium::P_STRING, 0},
  {corium::K_WEB_PASS, corium::L_WEB_PASS, SY_H_WPW, "",      nullptr, WEB_PASS_MIN, 0, WEB_PASS_MAX, corium::P_STRING, corium::P_SENSITIVE | corium::P_LOCAL | corium::P_REBOOT},
  {corium::K_ESPOTA,   SY_L_OTA, SY_H_OTA, "0",     nullptr, 0, 1, 0, corium::P_BOOL, corium::P_ADVANCED | corium::P_REBOOT},
  {corium::K_ORIGIN,   nullptr,  nullptr,  "1",     nullptr, 0, 1, 0, corium::P_BOOL, corium::P_HIDDEN},
  // max backoff s, 0 = every interval
  {corium::K_BACKOFF,  nullptr,  nullptr,  "300",   nullptr, 0, 3600, 0, corium::P_UINT, corium::P_HIDDEN},
  // 5 wrong logins = no logins taken for 10 s
  {corium::K_BRAKE,    nullptr,  nullptr,  "1",     nullptr, 0, 1, 0, corium::P_BOOL, corium::P_HIDDEN},
#ifdef ESP32
  // 0 = board default. C3 max 160
  {K_CPU_MHZ,     nullptr,  nullptr,  "0",     SY_O_MHZ, 0, 0,   0,  corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL},
#endif
};

static const corium::PrefGroup SYSTEM_PREF_GROUP PROGMEM = {
  "sys", "System", 1,
  SYSTEM_PREF_ITEMS,
  sizeof(SYSTEM_PREF_ITEMS) / sizeof(SYSTEM_PREF_ITEMS[0]),
};

const corium::PrefGroup* corium::SystemPrefs::prefsGroup() { return &SYSTEM_PREF_GROUP; }

bool corium::SystemPrefs::passRestored = false;
static bool s_unrestored = false;   // Put failed for memory, so the backup stands in for the login

static_assert(WEB_PASS_MAX <= corium::KeyStore::MAX, "web_pass fits a key store slot");

// Keeps the login, and if it can't save this runs again next boot
static void restore_pass(const char* kept) {
  if (!corium::Prefs::putKept("sys", corium::K_WEB_PASS, kept)) {
    s_unrestored = true;
    corium::Log::console(PSTR("System: Web password not restored, using the backup"));
    return;
  }
  if (!corium::Prefs::commit(false)) {
    corium::Log::console(PSTR("System: Web password not saved"));
    return;
  }
  corium::SystemPrefs::passRestored = true;
  corium::Log::console(PSTR("System: Web password restored from backup"));
}

static const char PASS_NOT_KEPT[] PROGMEM = "System: Web password backup not saved";

// Copy kept outside LittleFS, so a lost FS keeps the login. Only a save clears it
static void keep_pass(bool saved) {
  char kept[WEB_PASS_MAX + 1] = {};
  bool have = corium::KeyStore::read((uint8_t*)kept, WEB_PASS_MAX, WEB_PASS_SLOT);
  const char* cur = corium::Prefs::getString("sys", corium::K_WEB_PASS);
  if (have && strcmp(cur, kept) == 0) return;
  if (!cur[0]) {
    if (!have) return;
    if (!saved) restore_pass(kept);
    else if (!corium::KeyStore::clear(WEB_PASS_SLOT)) corium::Log::console(PASS_NOT_KEPT);
    return;
  }
  char pad[WEB_PASS_MAX];   // memset because = {} copies a ram template
  memset(pad, 0, sizeof(pad));
  memcpy(pad, cur, strnlen(cur, sizeof(pad)));
  if (!corium::KeyStore::write((const uint8_t*)pad, sizeof(pad), WEB_PASS_SLOT)) {
    corium::Log::console(PASS_NOT_KEPT);
  }
}

static void apply_sys() {
  corium::DeviceInfo::setFriendlyName(corium::Prefs::getString("sys", K_NAME));
#ifdef ESP32
  uint32_t mhz = corium::Prefs::getUInt("sys", K_CPU_MHZ);
  if (mhz) {
    // A C3 refuses 240, the log line below says so
    bool ok = setCpuFrequencyMhz(mhz);
    corium::Log::console(ok ? PSTR("System: CPU set to %u MHz") : PSTR("System: CPU %u MHz not taken, 80/160/240"),
                         (unsigned)mhz);
  }
#endif
  corium::WebPortal::applyAuthFromPrefs();
}

void corium::SystemPrefs::onPrefsLoaded() {
  keep_pass(false);
  apply_sys();
}

const char* corium::SystemPrefs::webPass(char* kept) {
  const char* cur = corium::Prefs::getString("sys", corium::K_WEB_PASS);
  if (*cur || !s_unrestored) return cur;
  memset(kept, 0, WEB_PASS_MAX + 1);
  if (!corium::KeyStore::read((uint8_t*)kept, WEB_PASS_MAX, WEB_PASS_SLOT)) kept[0] = '\0';
  return kept;
}

void corium::SystemPrefs::onPrefsSaved() {
  s_unrestored = false;   // a save decides what the login is
  keep_pass(true);
  apply_sys();
}

// Held prefs writes get flushed when due and at shutdown
static void prefs_wake(uint32_t at) { corium::Registry::dueAt(&sysprefs, at); }
static void prefs_lifecycle(corium::Lifecycle::Event e) {
  if (e != corium::Lifecycle::SHUTDOWN) return;
  corium::Prefs::flush(corium::fastMillis());
  corium::Fs::saveWear();
}

void corium::SystemPrefs::begin() {
  // If no lifecycle handler is free, every write goes straight out
  if (corium::Lifecycle::on(prefs_lifecycle)) corium::Prefs::holdBy(prefs_wake);
}

void corium::SystemPrefs::loop(unsigned long now) { corium::Prefs::flush(now); }

// Network prefs, hidden, edited on /wifi

class NetPrefs : public corium::Module {
public:
  const char* name() override { return PSTR("net"); }
  bool coreModule() override { return true; }
  uint8_t displayOrder() override { return 0; }
  const corium::PrefGroup* prefsGroup() override;
};

static NetPrefs netprefs;
CORIUM_REGISTER_MODULE(netprefs)

CORIUM_PSTR(NT_L_STA, "Use static IP");
CORIUM_PSTR(NT_L_DNS, "DNS server");
CORIUM_PSTR(NT_P_IP,  "[0-9.]+");

static const corium::Pref NET_PREF_ITEMS[] PROGMEM = {
  {corium::K_STATIC_IP, NT_L_STA, nullptr, "0", nullptr,  0, 0, 0,  corium::P_BOOL,   corium::P_LOCAL | corium::P_REBOOT},
  {corium::K_IP,        corium::L_IP, nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL | corium::P_REBOOT},
  {corium::K_GW,        corium::L_GW, nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL | corium::P_REBOOT},
  {corium::K_SN,        corium::L_SN, nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL | corium::P_REBOOT},
  {corium::K_DNS,       NT_L_DNS, nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL | corium::P_REBOOT},
};

static const corium::PrefGroup NET_PREF_GROUP PROGMEM = {
  "net", "Network", 1,
  NET_PREF_ITEMS,
  sizeof(NET_PREF_ITEMS) / sizeof(NET_PREF_ITEMS[0]),
};

const corium::PrefGroup* NetPrefs::prefsGroup() { return &NET_PREF_GROUP; }
