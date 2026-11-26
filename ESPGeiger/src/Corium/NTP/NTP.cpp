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
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/Clock.h"

corium::NTP_Client ntpclient = corium::NTP_Client();
CORIUM_REGISTER_MODULE(ntpclient)
#if defined(DISABLE_NTP)
// no sntp
#elif defined(ESP32)
#include "sntp.h"
#include <esp_netif.h>
#include <lwip/tcpip.h>

// Same as configTime() but without the lock. Runs on lwIP's thread, s_server outlives the call
static void sntp_start(void* server) {
  if (sntp_enabled()) sntp_stop();
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, (char*)server);
  sntp_init();
}

static void start_sntp(const char* server) {
  esp_netif_init();   // brings up lwIP's thread like configTime does, only once
  tcpip_callback(sntp_start, (void*)server);
}
#elif !defined(ESP8266)
static void start_sntp(const char*) {}   // host build has no lwIP thread
#endif

#ifndef DISABLE_NTP
CORIUM_PSTR(NT_L_SRV, "NTP server");
#endif
CORIUM_PSTR(NT_L_TZ,  "Timezone");
CORIUM_PSTR(NT_H_TZ,  "Olson name or POSIX rule");
CORIUM_PSTR(NT_P_TZ,  "[A-Za-z0-9/_+\\-,.:<>]+");

static const corium::Pref NTP_PREF_ITEMS[] PROGMEM = {
#ifndef DISABLE_NTP
  {corium::K_SERVER, NT_L_SRV, nullptr,  NTP_SERVER, corium::P_HOST, 0, 0, CORIUM_HOST_MAX, corium::P_STRING, corium::P_ADVANCED},
#endif
  {corium::K_TZ,     NT_L_TZ,  NT_H_TZ,  NTP_TZ,     NT_P_TZ,   0, 0, NTP_TZ_MAX, corium::P_STRING, 0},
};

#ifndef DISABLE_NTP
// sntp keeps the pointer, and a prefs save frees the pref's own string
static char s_server[CORIUM_HOST_MAX + 1];
#endif
// Set by sntp, the loop applies it
static volatile bool s_synced = false;

static const corium::PrefGroup NTP_PREF_GROUP PROGMEM = {
  "ntp", "NTP", 1,
  NTP_PREF_ITEMS,
  sizeof(NTP_PREF_ITEMS) / sizeof(NTP_PREF_ITEMS[0]),
};

const corium::PrefGroup* corium::NTP_Client::prefsGroup() { return &NTP_PREF_GROUP; }

// legacy import
static const corium::LegacyAlias NTP_LEGACY[] PROGMEM = {
#ifndef DISABLE_NTP
  {"srv", "server"},
#endif
  {"tz",  "tz"},
  {},
};
corium::LegacyRows corium::NTP_Client::legacyAliases() { return NTP_LEGACY; }

corium::NTP_Client::NTP_Client() {
}

void corium::NTP_Client::begin() {
  setup();
  corium::Registry::every(this, LOOP_MS);
}

static const char* posix_tz() {
  const char* tz = corium::Prefs::getString("ntp", corium::K_TZ);
  if (tz[0] == '\0') tz = NTP_TZ;
  const char* posix = EGTimeZone::rule(tz);
  if (posix) return posix;
  corium::Log::console(PSTR("NTP: Timezone '%s' not known, using UTC"), tz);
  return PSTR("UTC0");
}

static void set_tz(const char* posix) {
#ifdef ESP8266
  setTZ(posix);   // copies from flash
#else
  setenv("TZ", posix, 1);
  tzset();
#endif
}

// Runs before the modules begin, so a kept, RTC or browser clock shows local time with or without wifi
void corium::NTP_Client::onPrefsLoaded() {
  set_tz(posix_tz());
}

void corium::NTP_Client::setup()
{
#ifndef DISABLE_NTP
  const char* server = corium::Prefs::getString("ntp", corium::K_SERVER);
  if (server[0] == '\0') server = NTP_SERVER;
  if (strcmp(s_server, server) != 0) strlcpy(s_server, server, sizeof(s_server));
  server = s_server;

  corium::Log::console(PSTR("NTP: Starting ... %s"), server);
  const char *posix = posix_tz();
#ifdef ESP8266
  // An RTC's settimeofday fires this too, hence the from_sntp check
  settimeofday_cb([](bool from_sntp){
    if (!from_sntp || time(nullptr) < corium::Clock::MIN_EPOCH) return;  // SDK fires once before sync
    s_synced = true;
    corium::Registry::post(&ntpclient);
  });

  configTime(posix, server);
#else
  // Runs on the tcpip task, never wait here
  sntp_set_time_sync_notification_cb([](struct timeval *t){
    if (!t || t->tv_sec < corium::Clock::MIN_EPOCH) return;
    s_synced = true;
    corium::Registry::post(&ntpclient);
  });
  start_sntp(server);
  set_tz(posix);
#endif
#endif
}

void corium::NTP_Client::onPrefsSaved() {
  setup();
}

// Posted on each sync, otherwise runs every LOOP_MS to check for resyncs
void corium::NTP_Client::loop(unsigned long now) {
  if (s_synced) {
    s_synced = false;
    if (corium::Clock::onSync()) corium::Log::console(PSTR("NTP: Synched"));
  }
  if (corium::Clock::resyncDue(now)) setup();
  if (corium::Clock::synced) corium::Clock::refreshTzOffsetMin();
}
