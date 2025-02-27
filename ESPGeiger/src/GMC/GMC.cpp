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

GMC::GMC() {
}

void GMC::s_tick(unsigned long stick_now)
{
  if (stick_now - lastPing >= pingInterval)
  {
    if (lastPing == 0) {
      lastPing = stick_now;
      lastPing += random(30000);
      return;
    }
    lastPing = stick_now - (stick_now % 1000);
    GMC::postMeasurement();
  }
}

void GMC::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    if (request->responseHTTPcode() == 200)
    {
      String response = request->responseText();
      if (response.indexOf("OK") != -1) {
        Log::console(PSTR("GMC: Upload OK"));
      }
      else if (response.indexOf("ERR1") != -1) {
        Log::console(PSTR("GMC: User is not found"));
      }
      else if (response.indexOf("ERR2") != -1) {
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

  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    return;
  }

  const char* _api_id = configManager.getParamValueFromID("gmcAID");
  const char* _api_gc_id = configManager.getParamValueFromID("gmcGCID");

  if ((_api_id == NULL) && (_api_gc_id == NULL)) {
    return;
  }

#ifdef GEIGERTESTMODE
  Log::console(PSTR("GMC: Testmode"));
  return;
#endif

  if ((_api_id == NULL) || (_api_gc_id == NULL)) {
    Log::console(PSTR("GMC: Skipping upload, please set Account ID and GCID"));
    return;
  }

  Log::console(PSTR("GMC: Uploading latest data ..."));

  int avgcpm = gcounter.get_cpmf();
  float avgcpm5 = gcounter.get_cpm5f();
  float usv =  gcounter.get_usv5();
  char url[256];
  sprintf(url, GMC_URI, _api_id, _api_gc_id, avgcpm, avgcpm5, usv);

  static bool requestOpenResult;
  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone)
  {
    requestOpenResult = request.open("GET", url);
    if (requestOpenResult)
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
      Serial.println(PSTR("Can't send bad request"));
    }
  }
}
#endif
