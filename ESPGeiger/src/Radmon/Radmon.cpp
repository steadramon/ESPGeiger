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
#include "../Module/EGModuleRegistry.h"

extern uint8_t send_indicator;

Radmon radmon;
EG_REGISTER_MODULE(radmon)

static const EGPref RADMON_PREF_ITEMS[] = {
  {"send",     "Enable",   "Upload to radmon.org",  "0",  nullptr, 0, 0,  0,  EGP_BOOL,   0},
  {"user",     "Username", "",                      "",   nullptr, 0, 0,  32, EGP_STRING, 0},
  {"password", "Password", "",                      "",   nullptr, 0, 0,  64, EGP_STRING, EGP_SENSITIVE},
  {"interval", "Interval", "Upload interval (sec)", "60", nullptr, RADMON_INTERVAL_MIN, RADMON_INTERVAL_MAX, 0, EGP_UINT, 0},
};

static const EGPrefGroup RADMON_PREF_GROUP = {
  "radmon", "Radmon", 1,
  RADMON_PREF_ITEMS,
  sizeof(RADMON_PREF_ITEMS) / sizeof(RADMON_PREF_ITEMS[0]),
};

const EGPrefGroup* Radmon::prefs_group() { return &RADMON_PREF_GROUP; }

size_t Radmon::status_json(char* buf, size_t cap, unsigned long now) {
  if (!EGPrefs::getBool("radmon", "send")) return 0;
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

void Radmon::loop(unsigned long now)
{
  if (lastPing == 0) {
    int rtimer = (int)EGPrefs::getUInt("radmon", "interval");
    if (rtimer == 0) rtimer = RADMON_INTERVAL;
    setInterval(rtimer);
    lastPing = now + random(pingIntervalMs);
    return;
  }
  if (now > lastPing && (now - lastPing) >= pingIntervalMs)
  {
    // Advance by exact interval to keep the schedule drift-free.
    // If we were stalled long enough to still be >= one interval behind,
    // snap to now rather than firing a burst of catch-up publishes.
    lastPing += pingIntervalMs;
    if ((now - lastPing) >= pingIntervalMs) lastPing = now;
    postMeasurement();
  }
}

void Radmon::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    Radmon* self = static_cast<Radmon*>(optParm);
    self->last_attempt_ms = millis();
    self->last_ok = false;
    if (request->responseHTTPcode() == 200)
    {
      String response = request->responseText();
      const char* r = response.c_str();
      if (strstr(r, "OK")) {
        Log::debug(PSTR("Radmon: Upload OK"));
        self->last_ok = true;
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
    self->note_publish(self->last_ok);
  }
}

void Radmon::postMeasurement() {
  if (!EGPrefs::getBool("radmon", "send")) return;

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

  int rtimer = (int)EGPrefs::getUInt("radmon", "interval");
  if (rtimer == 0) rtimer = RADMON_INTERVAL;
  setInterval(rtimer);

  Log::debug(PSTR("Radmon: Uploading latest data ..."));

  int avgcpm;
  if (rtimer <= 90) {
    avgcpm = gcounter.get_cpm();
  } else if (rtimer <= 450) {
    avgcpm = gcounter.get_cpm5();
  } else {
    avgcpm = gcounter.get_cpm15();
  }
  char url[256];
  snprintf_P(url, sizeof(url), RADMON_URI, _api_user, _api_key, avgcpm);

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
      Log::console(PSTR("Radmon: Can't send request"));
    }
  }
}
#endif
