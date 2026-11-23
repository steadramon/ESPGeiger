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
#include "Counter/Counter.h"
#include "Corium/System/DeviceInfo.h"

Radmon radmon;
CORIUM_REGISTER_FEATURE(radmon, FEAT_RADMON)

CORIUM_PSTR(RM_H_EN,  "Upload to radmon.org");
CORIUM_PSTR(RM_L_USR, "Username");
CORIUM_PSTR(RM_K_HDR, "_send_hdr");
CORIUM_PSTR(RM_L_HDR, "Also send");
CORIUM_PSTR(RM_K_LOC, "loc");
CORIUM_PSTR(RM_L_LOC, "Location");
CORIUM_PSTR(RM_H_LOC, "The System location");
CORIUM_PSTR(RM_K_FUZ, "loc_fuzz");
CORIUM_PSTR(RM_L_FUZ, "Fuzz location");
CORIUM_PSTR(RM_H_FUZ, "To about 1 km, else 100 m");
CORIUM_PSTR(RM_K_FAC, "factor");
CORIUM_PSTR(RM_L_FAC, "Conversion factor");
CORIUM_PSTR(RM_H_FAC, "1 / ratio");
CORIUM_PSTR(RM_K_ALR, "alerts");
CORIUM_PSTR(RM_L_ALR, "Warn/alert");
CORIUM_PSTR(RM_H_ALR, "Warning and Alert CPM, both 0 = alerts off");


#define RADMON_USER_MAX 32
#define RADMON_PASS_MAX 64

static const corium::Pref RADMON_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND,     EG_L_EN,  RM_H_EN,  "0",  nullptr, 0, 0,  0,  corium::P_BOOL,   0},
  {EG_K_USER,     RM_L_USR, nullptr,  "",   nullptr, 0, 0,  RADMON_USER_MAX, corium::P_STRING, 0},
  {EG_K_PASSWORD, EG_L_PWD, nullptr,  "",   nullptr, 0, 0,  RADMON_PASS_MAX, corium::P_STRING, corium::P_SENSITIVE},
  {RM_K_HDR,      RM_L_HDR, nullptr,  nullptr, nullptr, 0, 0, 0, corium::P_LABEL, 0},
  {RM_K_LOC,      RM_L_LOC, RM_H_LOC, "0",  nullptr, 0, 0,  0,  corium::P_BOOL,   corium::P_INLINE},
  {RM_K_FAC,      RM_L_FAC, RM_H_FAC, "0",  nullptr, 0, 0,  0,  corium::P_BOOL,   corium::P_INLINE},
  {RM_K_ALR,      RM_L_ALR, RM_H_ALR, "0",  nullptr, 0, 0,  0,  corium::P_BOOL,   corium::P_INLINE},
  {RM_K_FUZ,      RM_L_FUZ, RM_H_FUZ, "1",  nullptr, 0, 0,  0,  corium::P_BOOL,   corium::P_ADVANCED},
  {EG_K_INTERVAL, EG_L_INT, EG_H_INT, STR(RADMON_INTERVAL), nullptr, RADMON_INTERVAL_MIN, RADMON_INTERVAL_MAX, 0, corium::P_UINT, corium::P_ADVANCED},
};

static const corium::PrefGroup RADMON_PREF_GROUP PROGMEM = {
  "radmon", "Radmon", 1,
  RADMON_PREF_ITEMS,
  sizeof(RADMON_PREF_ITEMS) / sizeof(RADMON_PREF_ITEMS[0]),
  corium::P_CAT_UPLOAD, EG_K_SEND, 0,
};

const corium::PrefGroup* Radmon::prefsGroup() { return &RADMON_PREF_GROUP; }

// legacy import
static const corium::LegacyAlias RADMON_LEGACY[] PROGMEM = {
  {"radmonSend", "send"},
  {"radmonUser", "user"},
  {"radmonKey",  "password"},
  {"radmonTime", "interval"},
  {},
};
corium::LegacyRows Radmon::legacyAliases() { return RADMON_LEGACY; }

Radmon::Radmon() {
  _every_ms = (uint32_t)RADMON_INTERVAL * 1000UL;
}

void Radmon::setInterval(int interval) {
  _interval_s = setIntervalS(interval);
}

int Radmon::getInterval() {
  return _interval_s;
}

void Radmon::onPrefsLoaded() {
  setInterval((int)corium::Prefs::getUInt("radmon", EG_K_INTERVAL));
  HttpPoster::onPrefsLoaded();
}

// "OK<br>" on success, else the error text
bool Radmon::interpret(const char* r)
{
  if (_set) {   // any 200
    corium::Log::console(_set == 1 ? PSTR("Radmon: Location sent") :
                         _set == 2 ? PSTR("Radmon: Factor sent") :
                         _set == 3 ? PSTR("Radmon: Warn/alert sent") :
                         _set == 4 ? PSTR("Radmon: Alerts on") : PSTR("Radmon: Alerts off"));
    return true;
  }
  if (strcmp_P(r, PSTR("OK<br>")) == 0) {
    corium::Log::debug(PSTR("Radmon: Upload OK"));
    return true;
  } else if (strstr_P(r, PSTR("Incorrect"))) {
    corium::Log::console(PSTR("Radmon: Password incorrect"));
  } else if (strstr_P(r, PSTR("register"))) {
    corium::Log::console(PSTR("Radmon: Username incorrect"));
  } else if (strstr_P(r, PSTR("Too soon"))) {
    corium::Log::console(PSTR("Radmon: Rate limited"));
  } else {
    corium::Log::console(PSTR("Radmon: Unknown error"));
  }
  return false;
}

// once after boot and after a change, tried once
bool Radmon::loc_due(float* lat, float* lon) {
  if (!corium::Prefs::getBool("radmon", RM_K_LOC)) return false;
  *lat = corium::Prefs::getFloat("loc", corium::K_LAT);
  *lon = corium::Prefs::getFloat("loc", corium::K_LON);
  if ((*lat == 0.0f && *lon == 0.0f) || (*lat == _lat && *lon == _lon)) return false;
  _lat = *lat;
  _lon = *lon;
  return true;
}

// 3 dp. Fuzzed: 2 dp, the third from the chip so a station sits apart in its cell, as StationsAPI does
static void loc_text(char* out, size_t cap, float v, uint32_t h, int32_t lim, bool fuzz) {
  int32_t t = fuzz ? (int32_t)(v * 100.0f + (v >= 0 ? 0.5f : -0.5f)) * 10 + (int32_t)(h % 10) - 5
                   : (int32_t)(v * 1000.0f + (v >= 0 ? 0.5f : -0.5f));
  if (t > lim * 1000) t = lim * 1000;
  if (t < -lim * 1000) t = -lim * 1000;
  format_f(out, cap, t / 1000.0f, 3);
}

// once after boot and after a change, each tried once. -1 = none due
int Radmon::setting(char* url, size_t cap, const char* user, const char* pass) {
  float lat, lon;
  if (loc_due(&lat, &lon)) {
    char la[12], lo[12];
    uint32_t h = corium::DeviceInfo::chipHash();
    bool fuzz = corium::Prefs::getBool("radmon", RM_K_FUZ);
    loc_text(la, sizeof(la), lat, h, 90, fuzz);
    loc_text(lo, sizeof(lo), lon, h / 10, 180, fuzz);
    _set = 1;
    return snprintf_P(url, cap, RADMON_LOC_URI, user, pass, la, lo);
  }
  float r = gcounter.get_ratio();
  if (corium::Prefs::getBool("radmon", RM_K_FAC) && r > 0 && r != _ratio) {
    _ratio = r;
    char v[12];
    format_f(v, sizeof(v), 1.0f / r, 6);
    _set = 2;
    return snprintf_P(url, cap, RADMON_FAC_URI, user, pass, v);
  }
  return corium::Prefs::getBool("radmon", RM_K_ALR) ? alerts(url, cap, user, pass) : -1;
}

// both set = the levels then alerts on, both 0 = alerts off, one 0 = nothing
int Radmon::alerts(char* url, size_t cap, const char* user, const char* pass) {
  int w = gcounter.get_warning(), a = gcounter.get_alert();
  bool changed = w != _warn || a != _alert;
  _warn = w;
  _alert = a;
  if (w > 0 && a > 0) {
    if (changed) {
      _on_due = true;
      _set = 3;
      return snprintf_P(url, cap, RADMON_ALR_URI, user, pass, (unsigned)w, (unsigned)a);
    }
    if (!_on_due) return -1;
    _on_due = false;
    _set = 4;
    return snprintf_P(url, cap, RADMON_AON_URI, user, pass);
  }
  _on_due = false;
  if (!changed) return -1;
  if (w || a) {
    corium::Log::console(PSTR("Radmon: Warn/alert need both set, not sent"));
    return -1;
  }
  _set = 5;
  return snprintf_P(url, cap, RADMON_AOFF_URI, user, pass);
}

bool Radmon::prepare(char* url, size_t cap, const char** /*body*/) {
  _set = 0;
  const char* raw_user = corium::Prefs::getString("radmon", EG_K_USER);
  const char* raw_pass = corium::Prefs::getString("radmon", EG_K_PASSWORD);
  if (unset(raw_user, RM_L_USR) || unset(raw_pass, EG_L_PWD)) return false;
  if (!upload_ready(logTag())) return false;

  corium::Log::debug(PSTR("Radmon: Uploading latest data ..."));

  // the average nearest the interval: 1, 5 or 15 min
  float avgcpm;
  if      (_interval_s <= 90)  avgcpm = gcounter.get_cpmf_stable();
  else if (_interval_s <= 450) avgcpm = gcounter.get_cpm5f();
  else                         avgcpm = gcounter.get_cpm15f();
  char value[12];
  format_f(value, sizeof(value), avgcpm, 1);
  char user[3 * RADMON_USER_MAX + 1], pass[3 * RADMON_PASS_MAX + 1];
  egesc_url(user, sizeof(user), raw_user);
  egesc_url(pass, sizeof(pass), raw_pass);
  bool read = _read_next;
  _read_next = false;
  int n = read ? -1 : setting(url, cap, user, pass);
  if (n >= 0) {
    if (urlFits(n, cap)) {
      _again = true;   // the reading right after, the next setting next slot
      _read_next = true;
      return true;
    }
    _set = 0;
  }
  return urlFits(snprintf_P(url, cap, RADMON_URI, user, pass, value), cap);
}
#endif
