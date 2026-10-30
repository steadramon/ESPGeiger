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
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/KeyStore.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/WebPortal/WebPortal.h"

EG_PSTR(K_NAME,           "name");
EG_PSTR(K_CPU_MHZ,        "cpu_mhz");

// System prefs

SystemPrefs sysprefs;
EG_REGISTER_MODULE(sysprefs)

EG_PSTR(SY_L_NAM, "Friendly name");
EG_PSTR(SY_H_NAM, "Optional label for web UI");
EG_PSTR(SY_L_WPW, "Web password");
EG_PSTR(SY_H_WPW, "Optional. Set to require login for the web UI (user: admin). Empty = no auth.");

static const EGPref SYSTEM_PREF_ITEMS[] PROGMEM = {
  {K_NAME,        SY_L_NAM, SY_H_NAM, "",      nullptr, 0, 0,    EG_NAME_MAX, EGP_STRING, 0},
  {EG_K_WEB_PASS, SY_L_WPW, SY_H_WPW, "",      nullptr, 0, 0,    WEB_PASS_MAX, EGP_STRING, EGP_SENSITIVE | EGP_ADVANCED | EGP_LOCAL | EGP_REBOOT},
#ifdef ESP32
  // 0 = board default. C3 max 160
  {K_CPU_MHZ,     nullptr,  nullptr,  "0",     nullptr, 0, 240,  0,  EGP_UINT,   EGP_HIDDEN | EGP_LOCAL},
#endif
};

static const EGPrefGroup SYSTEM_PREF_GROUP PROGMEM = {
  "sys", "System", 1,
  SYSTEM_PREF_ITEMS,
  sizeof(SYSTEM_PREF_ITEMS) / sizeof(SYSTEM_PREF_ITEMS[0]),
};

const EGPrefGroup* SystemPrefs::prefs_group() { return &SYSTEM_PREF_GROUP; }

bool SystemPrefs::pass_restored = false;

static_assert(WEB_PASS_MAX <= KeyStore::MAX, "web_pass fits a key store slot");

// a copy outside LittleFS, a lost FS keeps the login
static void keep_pass() {
  char kept[WEB_PASS_MAX + 1] = {};
  bool have = KeyStore::read((uint8_t*)kept, WEB_PASS_MAX, WEB_PASS_SLOT);
  if (!EGPrefs::stored("sys")) {
    if (have && EGPrefs::put("sys", EG_K_WEB_PASS, kept)) {
      // login holds, again next boot
      if (!EGPrefs::commit(false)) {
        Log::console(PSTR("System: Web password not saved"));
        return;
      }
      SystemPrefs::pass_restored = true;
      Log::console(PSTR("System: Web password restored from backup"));
    }
    return;
  }
  const char* cur = EGPrefs::getString("sys", EG_K_WEB_PASS);
  if (strcmp(cur, kept) == 0) return;
  if (!cur[0]) {
    KeyStore::clear(WEB_PASS_SLOT);
    return;
  }
  char pad[WEB_PASS_MAX];   // = {} copies a ram template
  memset(pad, 0, sizeof(pad));
  memcpy(pad, cur, strnlen(cur, sizeof(pad)));
  KeyStore::write((const uint8_t*)pad, sizeof(pad), WEB_PASS_SLOT);
}

void SystemPrefs::on_prefs_loaded() {
  keep_pass();
  DeviceInfo::setFriendlyName(EGPrefs::getString("sys", K_NAME));
#ifdef ESP32
  uint32_t mhz = EGPrefs::getUInt("sys", K_CPU_MHZ);
  if (mhz == 80 || mhz == 160 || mhz == 240) {
    if (setCpuFrequencyMhz(mhz)) {
      Log::console(PSTR("System: CPU set to %u MHz"), (unsigned)mhz);
    }
  }
#endif
  WebPortal::applyAuthFromPrefs();
}

// held prefs writes: flushed when due and at shutdown
static void prefs_wake(uint32_t at) { EGModuleRegistry::due_at(&sysprefs, at); }
static void prefs_lifecycle(Lifecycle::Event e) {
  if (e == Lifecycle::SHUTDOWN) EGPrefs::flush();
}

void SystemPrefs::begin() {
  // no handler free, every write goes now
  if (Lifecycle::on(prefs_lifecycle)) EGPrefs::hold_by(prefs_wake);
}

void SystemPrefs::loop(unsigned long) { EGPrefs::flush(); }

// Network prefs, hidden, edited on /wifi

class NetPrefs : public EGModule {
public:
  const char* name() override { return PSTR("net"); }
  bool core_module() override { return true; }
  uint8_t display_order() override { return 0; }
  const EGPrefGroup* prefs_group() override;
};

static NetPrefs netprefs;
EG_REGISTER_MODULE(netprefs)

EG_PSTR(NT_L_STA, "Use static IP");
EG_PSTR(NT_L_IP,  "IP address");
EG_PSTR(NT_L_GW,  "Gateway");
EG_PSTR(NT_L_SN,  "Subnet mask");
EG_PSTR(NT_L_DNS, "DNS server");
EG_PSTR(NT_P_IP,  "[0-9.]+");

static const EGPref NET_PREF_ITEMS[] PROGMEM = {
  {EG_K_STATIC_IP, NT_L_STA, nullptr, "0", nullptr,  0, 0, 0,  EGP_BOOL,   EGP_LOCAL},
  {EG_K_IP,        NT_L_IP,  nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, EGP_LOCAL},
  {EG_K_GW,        NT_L_GW,  nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, EGP_LOCAL},
  {EG_K_SN,        NT_L_SN,  nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, EGP_LOCAL},
  {EG_K_DNS,       NT_L_DNS, nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, EGP_LOCAL},
};

static const EGPrefGroup NET_PREF_GROUP PROGMEM = {
  "net", "Network", 1,
  NET_PREF_ITEMS,
  sizeof(NET_PREF_ITEMS) / sizeof(NET_PREF_ITEMS[0]),
};

const EGPrefGroup* NetPrefs::prefs_group() { return &NET_PREF_GROUP; }
