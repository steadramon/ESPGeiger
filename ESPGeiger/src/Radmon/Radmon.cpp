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
#include "Corium/Logger/Logger.h"
#include "Corium/Util/Words.h"
#include <EGEscape.h>
#include "Corium/Module/Registry.h"
#include "Corium/Measure/Measure.h"
#include "Counter/CounterUpload.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/MeasureIds.h"

Radmon radmon;
CORIUM_REGISTER_FEATURE(radmon, FEAT_RADMON)

CORIUM_PSTR(RM_H_EN,  "Upload to radmon.org");
CORIUM_PSTR(RM_L_USR, "Username");
CORIUM_PSTR(RM_H_INT, "Upload interval (sec)");


#define RADMON_USER_MAX 32
#define RADMON_PASS_MAX 64

static const corium::Pref RADMON_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND,     EG_L_EN,  RM_H_EN,  "0",  nullptr, 0, 0,  0,  corium::P_BOOL,   0},
  {EG_K_USER,     RM_L_USR, nullptr,  "",   nullptr, 0, 0,  RADMON_USER_MAX, corium::P_STRING, 0},
  {EG_K_PASSWORD, EG_L_PWD, nullptr,  "",   nullptr, 0, 0,  RADMON_PASS_MAX, corium::P_STRING, corium::P_SENSITIVE},
  {EG_K_INTERVAL, EG_L_INT, RM_H_INT, STR(RADMON_INTERVAL), nullptr, RADMON_INTERVAL_MIN, RADMON_INTERVAL_MAX, 0, corium::P_UINT, corium::P_ADVANCED},
};

static const corium::PrefGroup RADMON_PREF_GROUP PROGMEM = {
  "radmon", "Radmon", 1,
  RADMON_PREF_ITEMS,
  sizeof(RADMON_PREF_ITEMS) / sizeof(RADMON_PREF_ITEMS[0]),
  corium::P_CAT_UPLOAD, EG_K_SEND,
};

const corium::PrefGroup* Radmon::prefsGroup() { return &RADMON_PREF_GROUP; }

// === LEGACY IMPORT (remove in v1.1.0) ===
static const corium::LegacyAlias RADMON_LEGACY[] PROGMEM = {
  {"radmonSend", "send"},
  {"radmonUser", "user"},
  {"radmonKey",  "password"},
  {"radmonTime", "interval"},
  {},
};
corium::LegacyRows Radmon::legacyAliases() { return RADMON_LEGACY; }
// === END LEGACY IMPORT ===

Radmon::Radmon() {
  _every_ms = (uint32_t)RADMON_INTERVAL * 1000UL;
}

void Radmon::setInterval(int interval) {
  _interval_s = setIntervalS(interval, RADMON_INTERVAL_MIN, RADMON_INTERVAL_MAX);
}

int Radmon::getInterval() {
  return _interval_s;
}

void Radmon::onPrefsLoaded() {
  int iv = (int)corium::Prefs::getUInt("radmon", EG_K_INTERVAL);
  if (iv == 0) iv = RADMON_INTERVAL;
  setInterval(iv);
  setEnabled(corium::Prefs::getBool("radmon", EG_K_SEND));
}

// "OK<br>" on success, else the error text
bool Radmon::interpret(const char* r)
{
  if (strcmp_P(r, PSTR("OK<br>")) == 0) {
    corium::Log::debug(PSTR("Radmon: Upload OK"));
    return true;
  } else if (strstr_P(r, PSTR("Incorrect"))) {
    corium::Log::console(PSTR("Radmon: Password incorrect, please check"));
  } else if (strstr_P(r, PSTR("register"))) {
    corium::Log::console(PSTR("Radmon: Username incorrect, please check"));
  } else if (strstr_P(r, PSTR("Too soon"))) {
    corium::Log::console(PSTR("Radmon: Rate limited"));
  } else {
    corium::Log::console(PSTR("Radmon: Unknown error"));
  }
  return false;
}

bool Radmon::prepare(char* url, size_t cap, const char** /*body*/) {
  if (!upload_ready(logTag())) return false;

  const char* raw_user = corium::Prefs::getString("radmon", EG_K_USER);
  const char* raw_pass = corium::Prefs::getString("radmon", EG_K_PASSWORD);
  if (raw_user[0] == '\0' || raw_pass[0] == '\0') {
    corium::Log::console(PSTR("Radmon: Skipping upload, please set username and password"));
    return false;
  }

  corium::Log::debug(PSTR("Radmon: Uploading latest data ..."));

  // the average nearest the interval: 1, 5 or 15 min
  float avgcpm;
  if      (_interval_s <= 90)  avgcpm = corium::m_f(M_CPM_ST);
  else if (_interval_s <= 450) avgcpm = corium::m_f(M_CPM5);
  else                         avgcpm = corium::m_f(M_CPM15);
  char value[12];
  format_f(value, sizeof(value), avgcpm, 1);
  char user[3 * RADMON_USER_MAX + 1], pass[3 * RADMON_PASS_MAX + 1];
  egesc_url(user, sizeof(user), raw_user);
  egesc_url(pass, sizeof(pass), raw_pass);
  return urlFits(snprintf_P(url, cap, RADMON_URI, user, pass, value), cap);
}
#endif
