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
#include "Corium/System/KeyStore.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/WebPortal/WebPortal.h"

CORIUM_PSTR(K_NAME,           "name");
CORIUM_PSTR(K_CPU_MHZ,        "cpu_mhz");

// System prefs

corium::SystemPrefs sysprefs;
CORIUM_REGISTER_MODULE(sysprefs)

CORIUM_PSTR(SY_L_NAM, "Friendly name");
CORIUM_PSTR(SY_H_NAM, "Optional label for web UI");
CORIUM_PSTR(SY_L_WPW, "Web password");
CORIUM_PSTR(SY_H_WPW, "Optional. Set to require login for the web UI (user: admin), 8 or more characters. Empty = no auth. Reboot to apply.");

static const corium::Pref SYSTEM_PREF_ITEMS[] PROGMEM = {
  {K_NAME,        SY_L_NAM, SY_H_NAM, "",      nullptr, 0, 0,    CORIUM_NAME_MAX, corium::P_STRING, 0},
  {corium::K_WEB_PASS, SY_L_WPW, SY_H_WPW, "",      nullptr, WEB_PASS_MIN, 0, WEB_PASS_MAX, corium::P_STRING, corium::P_SENSITIVE | corium::P_ADVANCED | corium::P_LOCAL | corium::P_REBOOT},
#ifdef ESP32
  // 0 = board default. C3 max 160
  {K_CPU_MHZ,     nullptr,  nullptr,  "0",     nullptr, 0, 240,  0,  corium::P_UINT,   corium::P_HIDDEN | corium::P_LOCAL},
#endif
};

static const corium::PrefGroup SYSTEM_PREF_GROUP PROGMEM = {
  "sys", "System", 1,
  SYSTEM_PREF_ITEMS,
  sizeof(SYSTEM_PREF_ITEMS) / sizeof(SYSTEM_PREF_ITEMS[0]),
};

const corium::PrefGroup* corium::SystemPrefs::prefsGroup() { return &SYSTEM_PREF_GROUP; }

bool corium::SystemPrefs::passRestored = false;

static_assert(WEB_PASS_MAX <= corium::KeyStore::MAX, "web_pass fits a key store slot");

// login holds, again next boot
static void restore_pass(const char* kept) {
  if (!corium::Prefs::putKept("sys", corium::K_WEB_PASS, kept)) return;
  if (!corium::Prefs::commit(false)) {
    corium::Log::console(PSTR("System: Web password not saved"));
    return;
  }
  corium::SystemPrefs::passRestored = true;
  corium::Log::console(PSTR("System: Web password restored from backup"));
}

static const char PASS_NOT_KEPT[] PROGMEM = "System: Web password backup not saved";

// a copy outside LittleFS, a lost FS keeps the login. Only a save clears it,
// a blank or missing one at load takes it back
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
  char pad[WEB_PASS_MAX];   // = {} copies a ram template
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
  if (mhz == 80 || mhz == 160 || mhz == 240) {
    if (setCpuFrequencyMhz(mhz)) {
      corium::Log::console(PSTR("System: CPU set to %u MHz"), (unsigned)mhz);
    }
  }
#endif
  corium::WebPortal::applyAuthFromPrefs();
}

void corium::SystemPrefs::onPrefsLoaded() {
  keep_pass(false);
  apply_sys();
}

void corium::SystemPrefs::onPrefsSaved() {
  keep_pass(true);
  apply_sys();
}

// held prefs writes: flushed when due and at shutdown
static void prefs_wake(uint32_t at) { corium::Registry::dueAt(&sysprefs, at); }
static void prefs_lifecycle(corium::Lifecycle::Event e) {
  if (e == corium::Lifecycle::SHUTDOWN) corium::Prefs::flush(corium::fastMillis());
}

void corium::SystemPrefs::begin() {
  // no handler free, every write goes now
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
CORIUM_PSTR(NT_L_IP,  "IP address");
CORIUM_PSTR(NT_L_GW,  "Gateway");
CORIUM_PSTR(NT_L_SN,  "Subnet mask");
CORIUM_PSTR(NT_L_DNS, "DNS server");
CORIUM_PSTR(NT_P_IP,  "[0-9.]+");

static const corium::Pref NET_PREF_ITEMS[] PROGMEM = {
  {corium::K_STATIC_IP, NT_L_STA, nullptr, "0", nullptr,  0, 0, 0,  corium::P_BOOL,   corium::P_LOCAL},
  {corium::K_IP,        NT_L_IP,  nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL},
  {corium::K_GW,        NT_L_GW,  nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL},
  {corium::K_SN,        NT_L_SN,  nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL},
  {corium::K_DNS,       NT_L_DNS, nullptr, "",  NT_P_IP,  0, 0, CORIUM_IPV4_MAX, corium::P_STRING, corium::P_LOCAL},
};

static const corium::PrefGroup NET_PREF_GROUP PROGMEM = {
  "net", "Network", 1,
  NET_PREF_ITEMS,
  sizeof(NET_PREF_ITEMS) / sizeof(NET_PREF_ITEMS[0]),
};

const corium::PrefGroup* NetPrefs::prefsGroup() { return &NET_PREF_GROUP; }
