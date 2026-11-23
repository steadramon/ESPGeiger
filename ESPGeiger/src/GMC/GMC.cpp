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
#include <EGFormat.h>
#include <EGEscape.h>
#include "GMC.h"
#include "Counter/CounterMaths.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Util/Words.h"
#include "Corium/Module/Registry.h"
#include "Corium/Measure/Measure.h"
#include "Counter/CounterUpload.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/MeasureIds.h"
#include "Counter/Counter.h"

CORIUM_PSTR(K_AID,  "aid");
CORIUM_PSTR(K_GCID, "gcid");

GMC gmc;
CORIUM_REGISTER_FEATURE(gmc, FEAT_GMC)

CORIUM_PSTR(GM_H_EN,  "Upload to gmcmap.com");
CORIUM_PSTR(GM_L_AID, "Account ID");
CORIUM_PSTR(GM_H_AID, "gmcmap.com account ID");
CORIUM_PSTR(GM_P_ID,  "\\d{1,20}");
CORIUM_PSTR(GM_L_GID, "Geiger Counter ID");
CORIUM_PSTR(GM_H_GID, "gmcmap.com geiger counter ID");

#define GMC_ID_MAX 20

static const corium::Pref GMC_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND, EG_L_EN,  GM_H_EN,  "0", nullptr,  0, 0, 0,  corium::P_BOOL,   0},
  {K_AID,     GM_L_AID, GM_H_AID, "",  GM_P_ID,  0, 0, GMC_ID_MAX, corium::P_STRING, 0},   // NOLINT(sensitive-row) shown on the user's gmcmap page
  {K_GCID,    GM_L_GID, GM_H_GID, "",  GM_P_ID,  0, 0, GMC_ID_MAX, corium::P_STRING, corium::P_SENSITIVE},
};

static const corium::PrefGroup GMC_PREF_GROUP PROGMEM = {
  "gmc", "GMC", 1,
  GMC_PREF_ITEMS,
  sizeof(GMC_PREF_ITEMS) / sizeof(GMC_PREF_ITEMS[0]),
  corium::P_CAT_UPLOAD, EG_K_SEND, 0,
};

const corium::PrefGroup* GMC::prefsGroup() { return &GMC_PREF_GROUP; }

// legacy import
static const corium::LegacyAlias GMC_LEGACY[] PROGMEM = {
  {"gmcSend", "send"},
  {"gmcAID",  "aid"},
  {"gmcGCID", "gcid"},
  {},
};
corium::LegacyRows GMC::legacyAliases() { return GMC_LEGACY; }

GMC::GMC() {
  _every_ms = (uint32_t)GMC_INTERVAL * 1000UL;
}

// "OK.ERR0" on success
bool GMC::interpret(const char* r)
{
  size_t n = strlen(r);
  char code = (n >= 5 && strncmp_P(r + n - 5, PSTR(".ERR"), 4) == 0) ? r[n - 1] : 0;
  if (code == '0') {
    corium::Log::debug(PSTR("GMC: Upload OK"));
    return true;
  } else if (code == '1') {
    corium::Log::console(PSTR("GMC: User is not found"));
  } else if (code == '2') {
    corium::Log::console(PSTR("GMC: Geiger Counter is not found"));
  } else {
    corium::Log::console(PSTR("GMC: Error"));
  }
  return false;
}

bool GMC::prepare(char* url, size_t cap, const char** /*body*/) {
  const char* id   = corium::Prefs::getString("gmc", K_AID);
  const char* gcid = corium::Prefs::getString("gmc", K_GCID);
  if (unset(id, GM_L_AID) || unset(gcid, GM_L_GID)) return false;
  if (!upload_ready(logTag())) return false;

  corium::Log::debug(PSTR("GMC: Uploading latest data ..."));

  char aid[3 * GMC_ID_MAX + 1], gid[3 * GMC_ID_MAX + 1];
  egesc_url(aid, sizeof(aid), id);
  egesc_url(gid, sizeof(gid), gcid);
  // CPM is the 60 s bucket, ACPM and uSV the 5 min average
  int cpm = CounterMaths::to_int(gcounter.get_cpmf_stable());
  char acpm[16], usv[16];
  format_f(acpm, sizeof(acpm), gcounter.get_cpm5f());
  format_f(usv,  sizeof(usv),  gcounter.get_usv5(), 4);
  return urlFits(snprintf_P(url, cap, GMC_URI, aid, gid, cpm, acpm, usv), cap);
}
#endif
