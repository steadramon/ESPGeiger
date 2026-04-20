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
#include "GMC.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"

extern uint8_t send_indicator;

GMC gmc;
EG_REGISTER_MODULE(gmc)

static const EGPref GMC_PREF_ITEMS[] = {
  {"send", "Enable",            "Upload to gmcmap.com", "0", nullptr,       0, 0,  0,  EGP_BOOL,   0},
  {"aid",  "Account ID",        "gmcmap.com account ID", "",  "\\d{1,5}",    0, 0, 12, EGP_STRING, 0},
  {"gcid", "Geiger Counter ID", "gmcmap.com geiger counter ID", "", "\\d{1,12}", 0, 0, 12, EGP_STRING, 0},
};

static const EGPrefGroup GMC_PREF_GROUP = {
  "gmc", "GMC", 1,
  GMC_PREF_ITEMS,
  sizeof(GMC_PREF_ITEMS) / sizeof(GMC_PREF_ITEMS[0]),
};

const EGPrefGroup* GMC::prefs_group() { return &GMC_PREF_GROUP; }

size_t GMC::status_json(char* buf, size_t cap, unsigned long now) {
  if (!EGPrefs::getBool("gmc", "send")) return 0;
  return write_status_json(buf, cap, "gmc", last_ok, last_attempt_ms, now);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias GMC_LEGACY[] = {
  {"gmcSend", "send"},
  {"gmcAID",  "aid"},
  {"gmcGCID", "gcid"},
  {nullptr, nullptr},
};
const EGLegacyAlias* GMC::legacy_aliases() { return GMC_LEGACY; }
// === END LEGACY IMPORT ===

GMC::GMC() {
}

void GMC::loop(unsigned long now)
{
  if (lastPing == 0) {
    lastPing = now + random(pingIntervalMs);
    return;
  }
  if (now > lastPing && (now - lastPing) >= pingIntervalMs)
  {
    lastPing += pingIntervalMs;
    if ((now - lastPing) >= pingIntervalMs) lastPing = now;
    postMeasurement();
  }
}

void GMC::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    GMC* self = static_cast<GMC*>(optParm);
    self->last_attempt_ms = millis();
    self->last_ok = false;
    if (request->responseHTTPcode() == 200)
    {
      // strstr() on c_str() avoids the temp String alloc per indexOf probe.
      String response = request->responseText();
      const char* r = response.c_str();
      if (strstr(r, "OK")) {
        Log::debug(PSTR("GMC: Upload OK"));
        self->last_ok = true;
      } else if (strstr(r, "ERR1")) {
        Log::console(PSTR("GMC: User is not found"));
      } else if (strstr(r, "ERR2")) {
        Log::console(PSTR("GMC: Geiger Counter is not found"));
      } else {
        Log::console(PSTR("GMC: Error"));
      }
    } else {
      Log::console(PSTR("GMC: Error - %s"), request->responseHTTPString().c_str());
    }
    self->note_publish(self->last_ok);
  }
}

void GMC::postMeasurement() {
  if (!EGPrefs::getBool("gmc", "send")) return;

  const char* _api_id    = EGPrefs::getString("gmc", "aid");
  const char* _api_gc_id = EGPrefs::getString("gmc", "gcid");
  bool has_id   = (_api_id[0]    != '\0');
  bool has_gcid = (_api_gc_id[0] != '\0');

  // Nothing configured at all — stay silent so unconfigured units don't spam.
  if (!has_id && !has_gcid) return;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("GMC: Testmode"));
    return;
  }

  // Partially configured — log a hint.
  if (!has_id || !has_gcid) {
    Log::console(PSTR("GMC: Skipping upload, please set Account ID and GCID"));
    return;
  }

  Log::debug(PSTR("GMC: Uploading latest data ..."));

  int avgcpm = gcounter.get_cpmf();
  char acpm[16], usv[16];
  format_f(acpm, sizeof(acpm), gcounter.get_cpm5f());
  format_f(usv,  sizeof(usv),  gcounter.get_usv5(), 4);
  char url[256];
  snprintf(url, sizeof(url), GMC_URI, _api_id, _api_gc_id, avgcpm, acpm, usv);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone)
  {
    if (request.open("GET", url))
    {
      led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(30);
      request.send();
      send_indicator = 2;
    }
    else
    {
      Log::console(PSTR("GMC: Can't send request"));
    }
  }
}
#endif
