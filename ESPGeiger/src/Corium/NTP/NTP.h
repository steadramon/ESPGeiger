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
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"

#ifndef NTP_SERVER
#  define NTP_SERVER "pool.ntp.org"
#endif

#ifndef NTP_TZ
#  define NTP_TZ "Etc/UTC"
#endif


class NTP_Client : public EGModule {
  public:
  static NTP_Client &getInstance()
  {
    static NTP_Client instance;
    return instance;
  }
    NTP_Client();
    const char* name() override { return PSTR("ntp"); }
    uint8_t priority() override { return EG_PRIORITY_INFRASTRUCTURE; }
    bool requires_wifi() override { return true; }
    bool has_loop() override { return true; }
    bool has_tick() override { return true; }
    void s_tick(unsigned long now) override;
    uint16_t loop_interval_ms() override { return 60000; }
    void loop(unsigned long now) override;
    uint16_t warmup_seconds() override { return 0; }
    void begin() override { setup(); }
    void setup();
    const EGPrefGroup* prefs_group() override;
    void on_prefs_saved() override;
    void registerRoutes(EGHttpServer& http) override;
    uint8_t display_order() override { return 0; }  // managed via /network

    void renderInlineForm(class EGHttpResponse& res);
    EGLegacyRows legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    const char* legacy_file() override { return "/ntp.json"; }  // NOLINT(dram) LEGACY IMPORT

    const char* get_server() { return EGPrefs::getString("ntp", EG_K_SERVER); }
    const char* get_tz()     { return EGPrefs::getString("ntp", EG_K_TZ); }
};

extern NTP_Client ntpclient;

#endif
