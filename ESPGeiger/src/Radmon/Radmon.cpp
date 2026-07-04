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
#include "../Util/LedSignal.h"
#include "../Util/StringUtil.h"
#include "../Module/EGModuleRegistry.h"

extern uint8_t send_indicator;

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
  {"interval", RM_L_INT, RM_H_INT, "60", nullptr, RADMON_INTERVAL_MIN, RADMON_INTERVAL_MAX, 0, EGP_UINT, 0},
};

static const EGPrefGroup RADMON_PREF_GROUP = {
  "radmon", "Radmon", 1,
  RADMON_PREF_ITEMS,
  sizeof(RADMON_PREF_ITEMS) / sizeof(RADMON_PREF_ITEMS[0]),
  EGP_CAT_UPLOAD,
};

const EGPrefGroup* Radmon::prefs_group() { return &RADMON_PREF_GROUP; }

size_t Radmon::status_json(char* buf, size_t cap, unsigned long now) {
  if (!_send_enabled) return 0;
  return write_status_json(buf, cap, "radmon", last_ok, last_attempt_ms, now);
}

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
  _send_enabled = EGPrefs::getBool("radmon", "send");
  EGModuleRegistry::set_loop_interval(this, _send_enabled ? 500 : -1);
}

void Radmon::loop(unsigned long now)
{
  if (!_send_enabled) return;
  if (lastPing == 0) {
    lastPing = EGModuleRegistry::initial_ping(name(), now, pingIntervalMs);
  } else if ((now - lastPing) >= pingIntervalMs) {
    // Advance by whole intervals to preserve slot offset across catch-up.
    while ((now - lastPing) >= pingIntervalMs) lastPing += pingIntervalMs;
    postMeasurement();
  }
  EGModuleRegistry::sleep_until(this, now, lastPing + pingIntervalMs);
}

void Radmon::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    Radmon* self = static_cast<Radmon*>(optParm);
    bool ok = false;
    if (request->responseHTTPcode() == 200)
    {
      char r[64];
      size_t got = request->responseRead((uint8_t*)r, sizeof(r) - 1);
      r[got] = 0;
      if (strstr(r, "OK")) {
        Log::debug(PSTR("Radmon: Upload OK"));
        ok = true;
      } else if (strstr(r, "Incorrect")) {
        Log::console(PSTR("Radmon: Password incorrect, please check"));
      } else if (strstr(r, "register")) {
        Log::console(PSTR("Radmon: Username incorrect, please check"));
      } else if (strstr(r, "Too soon")) {
        Log::console(PSTR("Radmon: Rate limited"));
      } else {
        Log::console(PSTR("Radmon: Unknown error"));
      }
    } else {
      Log::console(PSTR("Radmon: Error - %s"), request->responseHTTPString().c_str());
    }
    self->note_result(ok);
  }
}

void Radmon::postMeasurement() {
  if (!gcounter.is_warm()) return;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Radmon: Testmode"));
    return;
  }

  const char* _api_user = EGPrefs::getString("radmon", "user");
  const char* _api_key  = EGPrefs::getString("radmon", "password");
  if (_api_user[0] == '\0' || _api_key[0] == '\0') {
    Log::console(PSTR("Radmon: Skipping upload, please set username and password"));
    return;
  }

  Log::debug(PSTR("Radmon: Uploading latest data ..."));

  float avgcpm = gcounter.get_cpmf_adaptive();
  char cpmbuf[12];
  format_f(cpmbuf, sizeof(cpmbuf), avgcpm, 1);
  char url[256];
  snprintf_P(url, sizeof(url), RADMON_URI, _api_user, _api_key, cpmbuf);

  if (!request) request = new AsyncHTTPRequest();
  if (!request) { Log::console(PSTR("Radmon: alloc failed")); return; }

  if (request->readyState() == readyStateUnsent || request->readyState() == readyStateDone)
  {
    if (request->open("GET", url))
    {
      LedSignal::activity();
      request->setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request->onReadyStateChange(httpRequestCb, this);
      request->setTimeout(30);
      request->send();
      note_attempt();
      send_indicator = 2;
    }
    else
    {
      Log::console(PSTR("Radmon: Can't send request"));
    }
  }
}
#endif
