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
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Util/StringUtil.h"
#include "../EnvSensor/EnvSensor.h"
#include <math.h>

Thingspeak thingspeak;
EG_REGISTER_MODULE(thingspeak)

EG_PSTR(TS_L_EN,  "Enable");
EG_PSTR(TS_H_EN,  "Upload to ThingSpeak");
EG_PSTR(TS_L_CK,  "Channel Key");
EG_PSTR(TS_H_CK,  "ThingSpeak channel write API key");

static const EGPref TS_PREF_ITEMS[] = {
  {"send",        TS_L_EN, TS_H_EN, "0", nullptr, 0, 0, 0,  EGP_BOOL,   0},
  {"channel_key", TS_L_CK, TS_H_CK, "",  nullptr, 0, 0, 16, EGP_STRING, EGP_SENSITIVE},
};

static const EGPrefGroup TS_PREF_GROUP = {
  "thingspeak", "ThingSpeak", 1,
  TS_PREF_ITEMS,
  sizeof(TS_PREF_ITEMS) / sizeof(TS_PREF_ITEMS[0]),
  EGP_CAT_UPLOAD, "send",
};

const EGPrefGroup* Thingspeak::prefs_group() { return &TS_PREF_GROUP; }

void Thingspeak::on_prefs_loaded() {
  set_enabled(EGPrefs::getBool("thingspeak", "send"));
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias TS_LEGACY[] = {
  {"tsSend",       "send"},
  {"tsChannelKey", "channel_key"},
  {nullptr, nullptr},
};
const EGLegacyAlias* Thingspeak::legacy_aliases() { return TS_LEGACY; }
// === END LEGACY IMPORT ===

Thingspeak::Thingspeak() {
  pingIntervalMs = (uint32_t)THINGSPEAK_INTERVAL * 1000UL;
}

bool Thingspeak::interpret(const char* r)
{
  if (strcmp(r, "0") != 0) {
    Log::debug(PSTR("Thingspeak: Upload OK"));
    return true;
  }
  Log::console(PSTR("Thingspeak: Error!"));
  return false;
}

bool Thingspeak::prepare(char* url, size_t cap, const char** /*body*/) {
  if (!gcounter.is_warm()) return false;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Thingspeak: Testmode"));
    return false;
  }

  const char* _ts_channel_key = EGPrefs::getString("thingspeak", "channel_key");
  if (_ts_channel_key[0] == '\0') return false;

  Log::debug(PSTR("Thingspeak: Uploading latest data ..."));

  int avgcpm = gcounter.get_cpm();
  int avgcpm5 = gcounter.get_cpm5();
  int avgcpm15 = gcounter.get_cpm15();

  float usv =  gcounter.get_usv();
  char usvChar[20];
  dtostrf(usv,1,5, usvChar);
  size_t up = snprintf_P(url, cap, TS_URI, _ts_channel_key, avgcpm, usvChar, avgcpm5, avgcpm15);
  // Append env fields when sensor present and the channel value is real.
  // Users with existing 4-field channels just leave 5/6/7 unconfigured.
  if (envsensor.present() && up < cap) {
    char fbuf[12];
    float et = envsensor.tempC(), eh = envsensor.humidity(), ep = envsensor.pressure();
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
  }

  return true;
}
#endif
