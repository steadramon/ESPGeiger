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
#ifndef NTP_h
#define NTP_h
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


class NTP_Client : public corium::Module {
  public:
    NTP_Client();
    const char* name() override { return PSTR("ntp"); }
    uint8_t priority() override { return CORIUM_PRIORITY_INFRASTRUCTURE; }
    bool requires_wifi() override { return true; }
    static constexpr uint16_t LOOP_MS = 60000;
    void loop(unsigned long now) override;
    void begin() override;
    void setup();
    const corium::PrefGroup* prefs_group() override;
    void on_prefs_saved() override;
    uint8_t display_order() override { return 0; }  // managed via /network

    corium::LegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove in v1.1.0)
    const char* legacy_file() override { return "/ntp.json"; }  // LEGACY IMPORT

    const char* get_server() { return corium::Prefs::getString("ntp", corium::K_SERVER); }
    const char* get_tz()     { return corium::Prefs::getString("ntp", corium::K_TZ); }
};

extern NTP_Client ntpclient;

#endif
