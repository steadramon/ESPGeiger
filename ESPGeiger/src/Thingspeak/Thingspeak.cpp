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

Thingspeak thingspeak;
EG_REGISTER_MODULE(thingspeak)

static const EGPref TS_PREF_ITEMS[] = {
  {"send",        "Enable",      "Upload to ThingSpeak", "0", nullptr, 0, 0,  0,  EGP_BOOL,   0},
  {"channel_key", "Channel Key", "ThingSpeak channel write API key", "",  nullptr, 0, 0, 16, EGP_STRING, EGP_SENSITIVE},
};

static const EGPrefGroup TS_PREF_GROUP = {
  "thingspeak", "ThingSpeak", 1,
  TS_PREF_ITEMS,
  sizeof(TS_PREF_ITEMS) / sizeof(TS_PREF_ITEMS[0]),
};

const EGPrefGroup* Thingspeak::prefs_group() { return &TS_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias TS_LEGACY[] = {
  {"tsSend",       "send"},
  {"tsChannelKey", "channel_key"},
  {nullptr, nullptr},
};
const EGLegacyAlias* Thingspeak::legacy_aliases() { return TS_LEGACY; }
// === END LEGACY IMPORT ===

Thingspeak::Thingspeak() {
}

void Thingspeak::loop(unsigned long now)
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

void Thingspeak::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    Thingspeak* self = static_cast<Thingspeak*>(optParm);
    self->last_attempt_ms = millis();
    self->last_ok = false;
    if (request->responseHTTPcode() == 200)
    {
      String response = request->responseText();
      if (strcmp(response.c_str(), "0") != 0) {
        Log::console(PSTR("Thingspeak: Upload OK"));
        self->last_ok = true;
      } else {
        Log::console(PSTR("Thingspeak: Error!"));
      }
    } else {
      Log::console(PSTR("Thingspeak: Error - %s"), request->responseHTTPString().c_str());
    }
  }
}

void Thingspeak::postMeasurement() {
  if (!EGPrefs::getBool("thingspeak", "send")) return;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Thingspeak: Testmode"));
    return;
  }

  const char* _ts_channel_key = EGPrefs::getString("thingspeak", "channel_key");
  if (_ts_channel_key[0] == '\0') return;

  Log::console(PSTR("Thingspeak: Uploading latest data ..."));

  int avgcpm = gcounter.get_cpm();
  int avgcpm5 = gcounter.get_cpm5();
  int avgcpm15 = gcounter.get_cpm15();

  float usv =  gcounter.get_usv();
  char usvChar[20];
  dtostrf(usv,1,5, usvChar);
  char url[256];
  snprintf(url, sizeof(url), TS_URI, _ts_channel_key, avgcpm, usvChar, avgcpm5, avgcpm15);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone)
  {
    if (request.open("GET", url))
    {
      status.led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(5);
      request.send();
      status.send_indicator = 2;
    }
    else
    {
      Log::console(PSTR("Thingspeak: Can't send request"));
    }
  }
}
#endif
