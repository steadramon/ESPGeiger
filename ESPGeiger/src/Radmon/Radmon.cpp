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
#include <EGFormat.h>
#include "../Corium/Logger/Logger.h"
#include "../Corium/Util/StringUtil.h"
#include "../Corium/Util/Words.h"
#include <EGEscape.h>
#include "../Corium/Module/EGModuleRegistry.h"
#include "../Corium/Measure/EGMeasure.h"

Radmon radmon;
EG_REGISTER_MODULE(radmon)

EG_PSTR(M_WARM,   "warm");
EG_PSTR(M_CPM_ST, "cpm_st");
EG_PSTR(M_CPM5,   "cpm5");
EG_PSTR(M_CPM15,  "cpm15");

EG_PSTR(RM_H_EN,  "Upload to radmon.org");
EG_PSTR(RM_L_USR, "Username");
EG_PSTR(RM_H_INT, "Upload interval (sec)");


#define RADMON_USER_MAX 32
#define RADMON_PASS_MAX 64

static const EGPref RADMON_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND,     EG_L_EN,  RM_H_EN,  "0",  nullptr, 0, 0,  0,  EGP_BOOL,   0},
  {EG_K_USER,     RM_L_USR, nullptr,  "",   nullptr, 0, 0,  RADMON_USER_MAX, EGP_STRING, 0},
  {EG_K_PASSWORD, EG_L_PWD, nullptr,  "",   nullptr, 0, 0,  RADMON_PASS_MAX, EGP_STRING, EGP_SENSITIVE},
  {EG_K_INTERVAL, EG_L_INT, RM_H_INT, STR(RADMON_INTERVAL), nullptr, RADMON_INTERVAL_MIN, RADMON_INTERVAL_MAX, 0, EGP_UINT, EGP_ADVANCED},
};

static const EGPrefGroup RADMON_PREF_GROUP PROGMEM = {
  "radmon", "Radmon", 1,
  RADMON_PREF_ITEMS,
  sizeof(RADMON_PREF_ITEMS) / sizeof(RADMON_PREF_ITEMS[0]),
  EGP_CAT_UPLOAD, EG_K_SEND,
};

const EGPrefGroup* Radmon::prefs_group() { return &RADMON_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias RADMON_LEGACY[] PROGMEM = {
  {"radmonSend", "send"},
  {"radmonUser", "user"},
  {"radmonKey",  "password"},
  {"radmonTime", "interval"},
  {},
};
EGLegacyRows Radmon::legacy_aliases() { return RADMON_LEGACY; }
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
  int rtimer = (int)EGPrefs::getUInt("radmon", EG_K_INTERVAL);
  if (rtimer == 0) rtimer = RADMON_INTERVAL;
  setInterval(rtimer);
  set_enabled(EGPrefs::getBool("radmon", EG_K_SEND));
}

bool Radmon::interpret(const char* r)
{
  if (strcmp_P(r, PSTR("OK<br>")) == 0) {
    Log::debug(PSTR("Radmon: Upload OK"));
    return true;
  } else if (strstr_P(r, PSTR("Incorrect"))) {
    Log::console(PSTR("Radmon: Password incorrect, please check"));
  } else if (strstr_P(r, PSTR("register"))) {
    Log::console(PSTR("Radmon: Username incorrect, please check"));
  } else if (strstr_P(r, PSTR("Too soon"))) {
    Log::console(PSTR("Radmon: Rate limited"));
  } else {
    Log::console(PSTR("Radmon: Unknown error"));
  }
  return false;
}

bool Radmon::prepare(char* url, size_t cap, const char** /*body*/) {
  if (!egm_b(M_WARM)) return false;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Radmon: Testmode"));
    return false;
  }

  const char* _api_user = EGPrefs::getString("radmon", EG_K_USER);
  const char* _api_key  = EGPrefs::getString("radmon", EG_K_PASSWORD);
  if (_api_user[0] == '\0' || _api_key[0] == '\0') {
    Log::console(PSTR("Radmon: Skipping upload, please set username and password"));
    return false;
  }

  Log::debug(PSTR("Radmon: Uploading latest data ..."));

  float avgcpm;
  if      (pingInterval <= 90)  avgcpm = egm_f(M_CPM_ST);
  else if (pingInterval <= 450) avgcpm = egm_f(M_CPM5);
  else                          avgcpm = egm_f(M_CPM15);
  char cpmbuf[12];
  format_f(cpmbuf, sizeof(cpmbuf), avgcpm, 1);
  char user[3 * RADMON_USER_MAX + 1], pass[3 * RADMON_PASS_MAX + 1];
  egesc_url(user, sizeof(user), _api_user);
  egesc_url(pass, sizeof(pass), _api_key);
  return url_fits(snprintf_P(url, cap, RADMON_URI, user, pass, cpmbuf), cap);
}
#endif
