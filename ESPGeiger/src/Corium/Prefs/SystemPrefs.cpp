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
  {EG_K_WEB_PASS, SY_L_WPW, SY_H_WPW, "",      nullptr, 0, 0,    WEB_PASS_MAX, EGP_STRING, EGP_SENSITIVE | EGP_ADVANCED},
#ifdef ESP32
  // 0 = board default. C3 max 160
  {K_CPU_MHZ,     nullptr,  nullptr,  "0",     nullptr, 0, 240,  0,  EGP_UINT,   EGP_HIDDEN},
#endif
};

static const EGPrefGroup SYSTEM_PREF_GROUP PROGMEM = {
  "sys", "System", 1,
  SYSTEM_PREF_ITEMS,
  sizeof(SYSTEM_PREF_ITEMS) / sizeof(SYSTEM_PREF_ITEMS[0]),
};

const EGPrefGroup* SystemPrefs::prefs_group() { return &SYSTEM_PREF_GROUP; }

void SystemPrefs::on_prefs_loaded() {
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
EG_PSTR(NT_P_IP,  "^(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)(\\.(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)){3}$");

static const EGPref NET_PREF_ITEMS[] PROGMEM = {
  {EG_K_STATIC_IP, NT_L_STA, nullptr, "0", nullptr,  0, 0, 0,  EGP_BOOL,   0},
  {EG_K_IP,        NT_L_IP,  nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, 0},
  {EG_K_GW,        NT_L_GW,  nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, 0},
  {EG_K_SN,        NT_L_SN,  nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, 0},
  {EG_K_DNS,       NT_L_DNS, nullptr, "",  NT_P_IP,  0, 0, EG_IPV4_MAX, EGP_STRING, 0},
};

static const EGPrefGroup NET_PREF_GROUP PROGMEM = {
  "net", "Network", 1,
  NET_PREF_ITEMS,
  sizeof(NET_PREF_ITEMS) / sizeof(NET_PREF_ITEMS[0]),
};

const EGPrefGroup* NetPrefs::prefs_group() { return &NET_PREF_GROUP; }
