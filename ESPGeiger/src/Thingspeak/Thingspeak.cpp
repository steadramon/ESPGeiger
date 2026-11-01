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

const corium::PrefGroup* Thingspeak::prefs_group() { return &TS_PREF_GROUP; }

void Thingspeak::on_prefs_loaded() {
  set_enabled(corium::Prefs::getBool("thingspeak", EG_K_SEND));
}

// === LEGACY IMPORT (remove in v1.1.0) ===
static const corium::LegacyAlias TS_LEGACY[] PROGMEM = {
  {"tsSend",       "send"},
  {"tsChannelKey", "channel_key"},
  {},
};
corium::LegacyRows Thingspeak::legacy_aliases() { return TS_LEGACY; }
// === END LEGACY IMPORT ===

Thingspeak::Thingspeak() {
  pingIntervalMs = (uint32_t)THINGSPEAK_INTERVAL * 1000UL;
}

// entry number, "0" = refused
bool Thingspeak::interpret(const char* r)
{
  bool digits = r[0] != '\0';
  for (const char* p = r; *p; p++) {
    if (*p < '0' || *p > '9') digits = false;
  }
  if (digits && strcmp_P(r, PSTR("0")) != 0) {
    Log::debug(PSTR("Thingspeak: Upload OK"));
    return true;
  }
  Log::console(PSTR("Thingspeak: Error!"));
  return false;
}

bool Thingspeak::prepare(char* url, size_t cap, const char** /*body*/) {
  if (!upload_ready(log_tag())) return false;

  const char* _ts_channel_key = corium::Prefs::getString("thingspeak", K_CHANNEL_KEY);
  if (_ts_channel_key[0] == '\0') return false;

  Log::debug(PSTR("Thingspeak: Uploading latest data ..."));

  int avgcpm = CounterMaths::to_int(corium::m_f(M_CPM));
  int avgcpm5 = CounterMaths::to_int(corium::m_f(M_CPM5));
  int avgcpm15 = CounterMaths::to_int(corium::m_f(M_CPM15));

  char usvChar[20];
  format_f(usvChar, sizeof(usvChar), corium::m_f(M_USV), 5);
  char key[3 * TS_KEY_MAX + 1];
  egesc_url(key, sizeof(key), _ts_channel_key);
  int n0 = snprintf_P(url, cap, TS_URI, key, avgcpm, usvChar, avgcpm5, avgcpm15);
  if (!url_fits(n0, cap)) return false;
  size_t up = (size_t)n0;
  char fbuf[12];
  float et = corium::m_f(M_T), eh = corium::m_f(M_H), ep = corium::m_f(M_P);
  int n;
  if (!isnan(et)) {
    format_f(fbuf, sizeof(fbuf), et);
    n = snprintf_P(url + up, cap - up, PSTR("&field5=%s"), fbuf);
    if (n > 0 && (size_t)n < cap - up) up += n;
  }
  if (!isnan(eh)) {
    format_f(fbuf, sizeof(fbuf), eh);
    n = snprintf_P(url + up, cap - up, PSTR("&field6=%s"), fbuf);
    if (n > 0 && (size_t)n < cap - up) up += n;
  }
  if (!isnan(ep)) {
    format_f(fbuf, sizeof(fbuf), ep);
    n = snprintf_P(url + up, cap - up, PSTR("&field7=%s"), fbuf);
    if (n > 0 && (size_t)n < cap - up) up += n;
  }

  return true;
}
#endif
