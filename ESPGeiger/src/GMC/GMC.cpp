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

GMC gmc;
EG_REGISTER_MODULE(gmc)

GMC::GMC() {
}

void GMC::s_tick(unsigned long stick_now)
{
  if (lastPing == 0) {
    lastPing = stick_now + random(pingInterval / 1000) * 1000;
    return;
  }
  if (stick_now > lastPing && stick_now - lastPing >= (unsigned long)pingInterval)
  {
    lastPing = stick_now - (stick_now % 1000);
    GMC::postMeasurement();
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
        Log::console(PSTR("GMC: Upload OK"));
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
  }
}

void GMC::postMeasurement() {
  ConfigManager &configManager = ConfigManager::getInstance();

  const char* _send = configManager.getParamValueFromID("gmcSend");
  if (_send == NULL || strcmp(_send, "N") == 0) {
    return;
  }

  const char* _api_id = configManager.getParamValueFromID("gmcAID");
  const char* _api_gc_id = configManager.getParamValueFromID("gmcGCID");

  // Nothing configured at all — stay silent so unconfigured units don't spam.
  if (_api_id == NULL && _api_gc_id == NULL) {
    return;
  }

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("GMC: Testmode"));
    return;
  }

  // Partially configured — log a hint.
  if (_api_id == NULL || _api_gc_id == NULL) {
    Log::console(PSTR("GMC: Skipping upload, please set Account ID and GCID"));
    return;
  }

  Log::console(PSTR("GMC: Uploading latest data ..."));

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
      status.led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), configManager.getUserAgent());
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(30);
      request.send();
      status.last_send = millis();
    }
    else
    {
      Log::console(PSTR("GMC: Can't send request"));
    }
  }
}
#endif
