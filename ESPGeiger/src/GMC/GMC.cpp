/*
  GMC.cpp - GMC class

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
#ifdef GMCOUT
#include <Arduino.h>
#include <EGFormat.h>
#include "GMC.h"
#include "../Corium/Logger/Logger.h"
#include "../Corium/Util/Words.h"
#include "../Corium/Module/EGModuleRegistry.h"
#include "../Corium/Measure/EGMeasure.h"

EG_PSTR(K_AID,  "aid");
EG_PSTR(K_GCID, "gcid");

GMC gmc;
EG_REGISTER_MODULE(gmc)

EG_PSTR(M_WARM,   "warm");
EG_PSTR(M_CPM_ST, "cpm_st");
EG_PSTR(M_CPM5,   "cpm5");
EG_PSTR(M_USV5,   "usv5");

EG_PSTR(GM_H_EN,  "Upload to gmcmap.com");
EG_PSTR(GM_L_AID, "Account ID");
EG_PSTR(GM_H_AID, "gmcmap.com account ID");
EG_PSTR(GM_P_ID,  "\\d{1,20}");
EG_PSTR(GM_L_GID, "Geiger Counter ID");
EG_PSTR(GM_H_GID, "gmcmap.com geiger counter ID");

static const EGPref GMC_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND, EG_L_EN,  GM_H_EN,  "0", nullptr,  0, 0, 0,  EGP_BOOL,   0},
  {K_AID,     GM_L_AID, GM_H_AID, "",  GM_P_ID,  0, 0, 20, EGP_STRING, 0},
  {K_GCID,    GM_L_GID, GM_H_GID, "",  GM_P_ID,  0, 0, 20, EGP_STRING, 0},
};

static const EGPrefGroup GMC_PREF_GROUP PROGMEM = {
  "gmc", "GMC", 1,
  GMC_PREF_ITEMS,
  sizeof(GMC_PREF_ITEMS) / sizeof(GMC_PREF_ITEMS[0]),
  EGP_CAT_UPLOAD, EG_K_SEND,
};

const EGPrefGroup* GMC::prefs_group() { return &GMC_PREF_GROUP; }

void GMC::on_prefs_loaded() {
  set_enabled(EGPrefs::getBool("gmc", EG_K_SEND));
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias GMC_LEGACY[] PROGMEM = {
  {"gmcSend", "send"},
  {"gmcAID",  "aid"},
  {"gmcGCID", "gcid"},
  {},
};
EGLegacyRows GMC::legacy_aliases() { return GMC_LEGACY; }
// === END LEGACY IMPORT ===

GMC::GMC() {
  pingIntervalMs = (uint32_t)GMC_INTERVAL * 1000UL;
}

// "OK.ERR0" on success
bool GMC::interpret(const char* r)
{
  size_t n = strlen(r);
  char code = (n >= 5 && strncmp_P(r + n - 5, PSTR(".ERR"), 4) == 0) ? r[n - 1] : 0;
  if (code == '0') {
    Log::debug(PSTR("GMC: Upload OK"));
    return true;
  } else if (code == '1') {
    Log::console(PSTR("GMC: User is not found"));
  } else if (code == '2') {
    Log::console(PSTR("GMC: Geiger Counter is not found"));
  } else {
    Log::console(PSTR("GMC: Error"));
  }
  return false;
}

bool GMC::prepare(char* url, size_t cap, const char** /*body*/) {
  if (!egm_b(M_WARM)) return false;

  const char* _api_id    = EGPrefs::getString("gmc", K_AID);
  const char* _api_gc_id = EGPrefs::getString("gmc", K_GCID);
  bool has_id   = (_api_id[0]    != '\0');
  bool has_gcid = (_api_gc_id[0] != '\0');

  if (!has_id && !has_gcid) return false;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("GMC: Testmode"));
    return false;
  }

  if (!has_id || !has_gcid) {
    Log::console(PSTR("GMC: Skipping upload, please set Account ID and GCID"));
    return false;
  }

  Log::debug(PSTR("GMC: Uploading latest data ..."));

  int avgcpm = (int)roundf(egm_f(M_CPM_ST));
  char acpm[16], usv[16];
  format_f(acpm, sizeof(acpm), egm_f(M_CPM5));
  format_f(usv,  sizeof(usv),  egm_f(M_USV5), 4);
  return url_fits(snprintf_P(url, cap, GMC_URI, _api_id, _api_gc_id, avgcpm, acpm, usv), cap);
}
#endif
