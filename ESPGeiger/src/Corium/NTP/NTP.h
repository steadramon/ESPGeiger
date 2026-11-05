/*
  NTP.h - NTP class

  Copyright (C) 2023 @steadramon

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
#ifndef CORIUM_NTP_H
#define CORIUM_NTP_H
#include <Arduino.h>
#include "Corium/Module/Module.h"
#include "Corium/Prefs/Prefs.h"

#ifndef NTP_SERVER
#  define NTP_SERVER "pool.ntp.org"
#endif

#ifndef NTP_TZ
#  define NTP_TZ "Etc/UTC"
#endif
#define NTP_TZ_MAX 64


namespace corium {
class NTP_Client : public Module {
  public:
    NTP_Client();
    const char* name() override { return PSTR("ntp"); }
    uint8_t priority() override { return CORIUM_PRIORITY_INFRASTRUCTURE; }
    bool requiresWifi() override { return true; }
    static constexpr uint16_t LOOP_MS = 60000;
    void loop(unsigned long now) override;
    void begin() override;
    void setup();
    const PrefGroup* prefsGroup() override;
    void onPrefsLoaded() override;
    void onPrefsSaved() override;
    uint8_t displayOrder() override { return 0; }  // managed via /network

    LegacyRows legacyAliases() override;  // LEGACY IMPORT (remove in v1.1.0)
    const char* legacyFile() override { return "/ntp.json"; }  // LEGACY IMPORT

    const char* getServer() { return Prefs::getString("ntp", K_SERVER); }
    const char* getTz()     { return Prefs::getString("ntp", K_TZ); }
};
}  // namespace corium

extern corium::NTP_Client ntpclient;

#endif
