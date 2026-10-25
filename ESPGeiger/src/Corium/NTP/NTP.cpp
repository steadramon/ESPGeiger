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
#include <time.h>
#include <sys/time.h>
#include <EGTimeZone.h>
#include "NTP.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/System/Clock.h"

NTP_Client ntpclient = NTP_Client();
EG_REGISTER_MODULE(ntpclient)
#ifdef ESP32
#include "sntp.h"
#endif

EG_PSTR(NT_L_SRV, "NTP Server");
EG_PSTR(NT_L_TZ,  "Timezone");
EG_PSTR(NT_H_TZ,  "Olson name");
EG_PSTR(NT_P_TZ,  "[A-Za-z0-9/_+\\-]+");

static const EGPref NTP_PREF_ITEMS[] PROGMEM = {
  {EG_K_SERVER, NT_L_SRV, nullptr,  NTP_SERVER, EG_P_HOST, 0, 0, EG_HOST_MAX, EGP_STRING, EGP_ADVANCED},
  {EG_K_TZ,     NT_L_TZ,  NT_H_TZ,  NTP_TZ,     NT_P_TZ,   0, 0, NTP_TZ_MAX, EGP_STRING, 0},
};

// sntp keeps the pointer, a save frees the pref's
static char s_server[EG_HOST_MAX + 1];
// set by sntp, the loop applies it
static volatile bool s_synced = false;

static const EGPrefGroup NTP_PREF_GROUP PROGMEM = {
  "ntp", "NTP", 1,
  NTP_PREF_ITEMS,
  sizeof(NTP_PREF_ITEMS) / sizeof(NTP_PREF_ITEMS[0]),
};

const EGPrefGroup* NTP_Client::prefs_group() { return &NTP_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias NTP_LEGACY[] PROGMEM = {
  {"srv", "server"},
  {"tz",  "tz"},
  {},
};
EGLegacyRows NTP_Client::legacy_aliases() { return NTP_LEGACY; }
// === END LEGACY IMPORT ===

NTP_Client::NTP_Client() {
}

void NTP_Client::begin() {
  setup();
  EGModuleRegistry::every(this, LOOP_MS);
}

void NTP_Client::setup()
{
#ifndef DISABLE_NTP
  const char* server = EGPrefs::getString("ntp", EG_K_SERVER);
  if (server[0] == '\0') server = NTP_SERVER;
  if (strcmp(s_server, server) != 0) strlcpy(s_server, server, sizeof(s_server));
  server = s_server;
  const char* tz = EGPrefs::getString("ntp", EG_K_TZ);
  if (tz[0] == '\0') tz = NTP_TZ;

  Log::console(PSTR("NTP: Starting ... %s"), server);
  const char *possixTZ = EGTimeZone::posixFor(tz);
  if (!possixTZ) {
    Log::console(PSTR("NTP: timezone '%s' not known, using UTC"), tz);
    possixTZ = PSTR("UTC0");
  }
#ifdef ESP8266
  settimeofday_cb([](){
    if (Clock::epoch() < Clock::MIN_EPOCH) return;  // SDK fires once before sync
    s_synced = true;
    EGModuleRegistry::post(&ntpclient);
  });

  configTime(possixTZ, server);
#else
  // tcpip task, never wait here
  sntp_set_time_sync_notification_cb([](struct timeval *t){
    if (!t || (uint32_t)t->tv_sec < Clock::MIN_EPOCH) return;
    s_synced = true;
    EGModuleRegistry::post(&ntpclient);
  });
  configTime(0, 0, server); // TZ set below
  setenv("TZ", possixTZ, 1); // Set environment variable with your time zone
  tzset();
#endif
#endif
}

void NTP_Client::on_prefs_saved() {
  setup();
}

// posted on each sync, else every LOOP_MS for resyncs
void NTP_Client::loop(unsigned long now) {
  if (s_synced) {
    s_synced = false;
    if (Clock::on_sync()) Log::console(PSTR("NTP: Synched"));
  }
  if (Clock::resync_due(now)) setup();
  if (Clock::synced) Clock::refresh_tz_offset_min();
}
