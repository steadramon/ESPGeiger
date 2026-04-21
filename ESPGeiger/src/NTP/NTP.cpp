/*
  NTP.cpp - functions to handle NTP

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
#include <Arduino.h>
#include "NTP.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"

NTP_Client ntpclient = NTP_Client();
EG_REGISTER_MODULE(ntpclient)
#ifdef ESP32
#include "sntp.h"
#endif

static const EGPref NTP_PREF_ITEMS[] = {
  {"server", "NTP Server", "",            NTP_SERVER, nullptr, 0, 0, 64, EGP_STRING, 0},
  {"tz",     "Timezone",   "Olson name",  NTP_TZ,     nullptr, 0, 0, 64, EGP_STRING, 0},
};

static const EGPrefGroup NTP_PREF_GROUP = {
  "ntp", "NTP", 1,
  NTP_PREF_ITEMS,
  sizeof(NTP_PREF_ITEMS) / sizeof(NTP_PREF_ITEMS[0]),
};

const EGPrefGroup* NTP_Client::prefs_group() { return &NTP_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias NTP_LEGACY[] = {
  {"srv", "server"},
  {"tz",  "tz"},
  {nullptr, nullptr},
};
const EGLegacyAlias* NTP_Client::legacy_aliases() { return NTP_LEGACY; }
// === END LEGACY IMPORT ===

NTP_Client::NTP_Client() {
}

void NTP_Client::setup()
{
#ifndef DISABLE_NTP
  const char* server = EGPrefs::getString("ntp", "server");
  if (server[0] == '\0') server = NTP_SERVER;
  const char* tz = EGPrefs::getString("ntp", "tz");
  if (tz[0] == '\0') tz = NTP_TZ;

  Log::console(PSTR("NTP: Starting ... %s"), server);
  const char *possixTZ = getPosixTZforOlson(tz);
#ifdef ESP8266
  settimeofday_cb([](){
    if (!ntpclient.synced) {
      ntpclient.synced = true;
      unsigned long uptime = NTP.getUptime () - start;
      ntpclient.boot_epoch = int(time(NULL)-uptime);
      Log::console(PSTR("NTP: Synched"));
    }
  });

  configTime(possixTZ, server);
#else
  sntp_set_time_sync_notification_cb([](struct timeval *t){
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)){
      return;
    }
    if (!ntpclient.synced) {
      ntpclient.synced = true;
      unsigned long uptime = NTP.getUptime () - start;
      ntpclient.boot_epoch = int(time(NULL)-uptime);
      Log::console(PSTR("NTP: Synched"));
    }
  });
  configTime(0, 0, server); // 0, 0 because we will use TZ in the next line
  setenv("TZ", possixTZ, 1); // Set environment variable with your time zone
  tzset();
#endif
#endif
}

void NTP_Client::on_prefs_saved() {
  setup();  // re-apply server/tz without reboot
}
