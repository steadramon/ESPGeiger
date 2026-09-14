/*
  Radmon.cpp - Radmon class

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
#ifdef RADMONOUT
#include "Radmon.h"
#include "../Logger/Logger.h"
#include "../Util/StringUtil.h"
#include "../Module/EGModuleRegistry.h"

Radmon radmon;
EG_REGISTER_MODULE(radmon)

EG_PSTR(RM_L_EN,  "Enable");
EG_PSTR(RM_H_EN,  "Upload to radmon.org");
EG_PSTR(RM_L_USR, "Username");
EG_PSTR(RM_L_PWD, "Password");
EG_PSTR(RM_L_INT, "Interval");
EG_PSTR(RM_H_INT, "Upload interval (sec)");

static const EGPref RADMON_PREF_ITEMS[] = {
  {"send",     RM_L_EN,  RM_H_EN,  "0",  nullptr, 0, 0,  0,  EGP_BOOL,   0},
  {"user",     RM_L_USR, nullptr,  "",   nullptr, 0, 0,  32, EGP_STRING, 0},
  {"password", RM_L_PWD, nullptr,  "",   nullptr, 0, 0,  64, EGP_STRING, EGP_SENSITIVE},
  {"interval", RM_L_INT, RM_H_INT, "60", nullptr, RADMON_INTERVAL_MIN, RADMON_INTERVAL_MAX, 0, EGP_UINT, EGP_ADVANCED},
};

static const EGPrefGroup RADMON_PREF_GROUP = {
  "radmon", "Radmon", 1,
  RADMON_PREF_ITEMS,
  sizeof(RADMON_PREF_ITEMS) / sizeof(RADMON_PREF_ITEMS[0]),
  EGP_CAT_UPLOAD, "send",
};

const EGPrefGroup* Radmon::prefs_group() { return &RADMON_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias RADMON_LEGACY[] = {
  {"radmonSend", "send"},
  {"radmonUser", "user"},
  {"radmonKey",  "password"},
  {"radmonTime", "interval"},
  {nullptr, nullptr},
};
const EGLegacyAlias* Radmon::legacy_aliases() { return RADMON_LEGACY; }
// === END LEGACY IMPORT ===

Radmon::Radmon() {
  pingIntervalMs = (uint32_t)RADMON_INTERVAL * 1000UL;
}

void Radmon::setInterval(int interval) {
  if (interval == pingInterval) {
    return;
  }
  if (interval > RADMON_INTERVAL_MAX) {
    interval = RADMON_INTERVAL_MAX;
  }
  if (interval < RADMON_INTERVAL_MIN) {
    interval = RADMON_INTERVAL_MIN;
  }
  pingInterval = interval;
  pingIntervalMs = (uint32_t)interval * 1000UL;
}

int Radmon::getInterval() {
  return pingInterval;
}

void Radmon::on_prefs_loaded() {
  int rtimer = (int)EGPrefs::getUInt("radmon", "interval");
  if (rtimer == 0) rtimer = RADMON_INTERVAL;
  setInterval(rtimer);
  set_enabled(EGPrefs::getBool("radmon", "send"));
}

bool Radmon::interpret(const char* r)
{
  if (strstr(r, "OK")) {
    Log::debug(PSTR("Radmon: Upload OK"));
    return true;
  } else if (strstr(r, "Incorrect")) {
    Log::console(PSTR("Radmon: Password incorrect, please check"));
  } else if (strstr(r, "register")) {
    Log::console(PSTR("Radmon: Username incorrect, please check"));
  } else if (strstr(r, "Too soon")) {
    Log::console(PSTR("Radmon: Rate limited"));
  } else {
    Log::console(PSTR("Radmon: Unknown error"));
  }
  return false;
}

bool Radmon::prepare(char* url, size_t cap, const char** /*body*/) {
  if (!gcounter.is_warm()) return false;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Radmon: Testmode"));
    return false;
  }

  const char* _api_user = EGPrefs::getString("radmon", "user");
  const char* _api_key  = EGPrefs::getString("radmon", "password");
  if (_api_user[0] == '\0' || _api_key[0] == '\0') {
    Log::console(PSTR("Radmon: Skipping upload, please set username and password"));
    return false;
  }

  Log::debug(PSTR("Radmon: Uploading latest data ..."));

  float avgcpm;
  if      (pingInterval <= 90)  avgcpm = gcounter.get_cpmf_stable();
  else if (pingInterval <= 450) avgcpm = gcounter.get_cpm5f();
  else                          avgcpm = gcounter.get_cpm15f();
  char cpmbuf[12];
  format_f(cpmbuf, sizeof(cpmbuf), avgcpm, 1);
  snprintf_P(url, cap, RADMON_URI, _api_user, _api_key, cpmbuf);
  return true;
}
#endif
