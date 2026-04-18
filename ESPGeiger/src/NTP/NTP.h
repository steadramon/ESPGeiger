/*
  NTP.h - NTP class

  Copyright (C) 2023 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef _NTP_h
#define _NTP_h
#include <Arduino.h>
#include <ESPNtpClient.h>
#include "timezones.h"
#include "../Status.h"
#include "../Module/EGModule.h"
#include "../Prefs/EGPrefs.h"

#ifndef NTP_SERVER
#  define NTP_SERVER "pool.ntp.org"
#endif

#ifndef NTP_TZ
#  define NTP_TZ "Etc/UTC"
#endif

extern Status status;

class NTP_Client : public EGModule {
  public:
  static NTP_Client &getInstance()
  {
    static NTP_Client instance;
    return instance;
  }
    NTP_Client();
    const char* name() override { return "ntp"; }
    uint8_t priority() override { return EG_PRIORITY_INFRASTRUCTURE; }
    bool requires_wifi() override { return true; }
    uint16_t warmup_seconds() override { return 0; }
    void begin() override { setup(); }
    void setup();
    const EGPrefGroup* prefs_group() override;
    void on_prefs_saved() override;  // reconfigures NTP client live
    uint8_t display_order() override { return 0; }  // managed via /ntp page
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
    const char* legacy_file() override { return "/ntp.json"; }  // LEGACY IMPORT

    const char* get_server() { return EGPrefs::getString("ntp", "server"); }
    const char* get_tz()     { return EGPrefs::getString("ntp", "tz"); }
};

#endif
