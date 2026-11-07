/*
  Thingspeak.cpp - Thingspeak class

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
#ifdef THINGSPEAKOUT
#include "Thingspeak.h"
#include <EGFormat.h>
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Util/StringUtil.h"
#include <EGEscape.h>
#include "Corium/Measure/Measure.h"
#include "Counter/CounterUpload.h"
#include "Counter/CounterMaths.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/MeasureIds.h"
#include <math.h>

CORIUM_PSTR(K_CHANNEL_KEY, "channel_key");

Thingspeak thingspeak;
CORIUM_REGISTER_FEATURE(thingspeak, FEAT_THINGSPEAK)

CORIUM_PSTR(TS_H_EN,  "Upload to ThingSpeak");
CORIUM_PSTR(TS_L_CK,  "Channel Key");
CORIUM_PSTR(TS_H_CK,  "ThingSpeak channel write API key");

#define TS_KEY_MAX 16

static const corium::Pref TS_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND,     EG_L_EN, TS_H_EN, "0", nullptr, 0, 0, 0,  corium::P_BOOL,   0},
  {K_CHANNEL_KEY, TS_L_CK, TS_H_CK, "",  nullptr, 0, 0, TS_KEY_MAX, corium::P_STRING, corium::P_SENSITIVE},
};

static const corium::PrefGroup TS_PREF_GROUP PROGMEM = {
  "thingspeak", "ThingSpeak", 1,
  TS_PREF_ITEMS,
  sizeof(TS_PREF_ITEMS) / sizeof(TS_PREF_ITEMS[0]),
  corium::P_CAT_UPLOAD, EG_K_SEND,
};

const corium::PrefGroup* Thingspeak::prefsGroup() { return &TS_PREF_GROUP; }

void Thingspeak::onPrefsLoaded() {
  setEnabled(corium::Prefs::getBool("thingspeak", EG_K_SEND));
}

// === LEGACY IMPORT (remove in v1.1.0) ===
static const corium::LegacyAlias TS_LEGACY[] PROGMEM = {
  {"tsSend",       "send"},
  {"tsChannelKey", "channel_key"},
  {},
};
corium::LegacyRows Thingspeak::legacyAliases() { return TS_LEGACY; }
// === END LEGACY IMPORT ===

Thingspeak::Thingspeak() {
  _every_ms = (uint32_t)THINGSPEAK_INTERVAL * 1000UL;
}

// entry number, "0" = refused
bool Thingspeak::interpret(const char* r)
{
  bool digits = r[0] != '\0';
  for (const char* p = r; *p; p++) {
    if (*p < '0' || *p > '9') digits = false;
  }
  if (digits && strcmp_P(r, PSTR("0")) != 0) {
    corium::Log::debug(PSTR("Thingspeak: Upload OK"));
    return true;
  }
  corium::Log::console(PSTR("Thingspeak: Error!"));
  return false;
}

// &fieldN=v, no reading leaves it out
static size_t add_field(char* url, size_t cap, size_t pos, unsigned field, float v) {
  if (isnan(v)) return pos;
  char num[12];
  format_f(num, sizeof(num), v);
  int n = snprintf_P(url + pos, cap - pos, PSTR("&field%u=%s"), field, num);
  return (n > 0 && (size_t)n < cap - pos) ? pos + (size_t)n : pos;
}

bool Thingspeak::prepare(char* url, size_t cap, const char** /*body*/) {
  if (!upload_ready(logTag())) return false;

  const char* raw_key = corium::Prefs::getString("thingspeak", K_CHANNEL_KEY);
  if (raw_key[0] == '\0') return false;

  corium::Log::debug(PSTR("Thingspeak: Uploading latest data ..."));

  int cpm = CounterMaths::to_int(corium::m_f(M_CPM));
  int cpm5 = CounterMaths::to_int(corium::m_f(M_CPM5));
  int cpm15 = CounterMaths::to_int(corium::m_f(M_CPM15));

  char usv[20];
  format_f(usv, sizeof(usv), corium::m_f(M_USV), 5);
  char key[3 * TS_KEY_MAX + 1];
  egesc_url(key, sizeof(key), raw_key);
  int n0 = snprintf_P(url, cap, TS_URI, key, cpm, usv, cpm5, cpm15);
  if (!urlFits(n0, cap)) return false;
  size_t pos = (size_t)n0;
  pos = add_field(url, cap, pos, 5, corium::m_f(M_T));
  pos = add_field(url, cap, pos, 6, corium::m_f(M_H));
  pos = add_field(url, cap, pos, 7, corium::m_f(M_P));
  return true;
}
#endif
