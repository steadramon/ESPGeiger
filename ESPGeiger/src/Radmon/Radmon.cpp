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

Radmon::Radmon() {
}

void Radmon::setInterval(int interval) {
  if (interval * 1000 == pingInterval) {
    return;
  }
  if (interval > RADMON_INTERVAL_MAX) {
    interval = RADMON_INTERVAL_MAX;
  }
  if (interval < RADMON_INTERVAL_MIN) {
    interval = RADMON_INTERVAL_MIN;
  }
  pingInterval = interval*1000;
}

int Radmon::getInterval() {
  return pingInterval / 1000;
}

void Radmon::s_tick(unsigned long stick_now)
{
  if (stick_now - lastPing >= pingInterval)
  {
    if (lastPing == 0) {
      ConfigManager &configManager = ConfigManager::getInstance();
      int rtimer = atoi(configManager.getParamValueFromID("radmonTime"));
      if (rtimer == NULL) {
        rtimer = RADMON_INTERVAL;
      }
      setInterval(rtimer);
      lastPing = random(rtimer / 2) * 1000;
      return;
    }
    lastPing = stick_now - (stick_now % 1000);
    Radmon::postMeasurement();
  }
}

void Radmon::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    if (request->responseHTTPcode() == 200)
    {
      String response = request->responseText();
      if (response.indexOf("OK") != -1) {
        Log::console(PSTR("Radmon: Upload OK"));
      }
      else if (response.indexOf("Incorrect") != -1) {
        Log::console(PSTR("Radmon: Password incorrect, please check"));
      }
      else if (response.indexOf("register") != -1) {
        Log::console(PSTR("Radmon: Username incorrect, please check"));
      }
      else {
        Log::console(PSTR("Radmon: Unknown error"));
      }
    } else {
      Log::console(PSTR("Radmon: Error - %s"), request->responseHTTPString().c_str());
    }
  }
}

void Radmon::postMeasurement() {

  ConfigManager &configManager = ConfigManager::getInstance();

  const char* _send = configManager.getParamValueFromID("radmonSend");

  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    return;
  }

  const char* _api_user = configManager.getParamValueFromID("radmonUser");
  const char* _api_key = configManager.getParamValueFromID("radmonKey");
  int rtimer = atoi(configManager.getParamValueFromID("radmonTime"));
  if (rtimer == NULL) {
    rtimer = RADMON_INTERVAL;
  }
  setInterval(rtimer);

  if ((_api_user == NULL) && (_api_key == NULL)) {
    return;
  }

#ifdef GEIGERTESTMODE
  Log::console(PSTR("Radmon: Testmode"));
  return;
#endif

  if ((_api_user == NULL) || (_api_key == NULL)) {
    Log::console(PSTR("Radmon: Skipping upload, please set username and password"));
    return;
  }

  Log::console(PSTR("Radmon: Uploading latest data ..."));

  int avgcpm;
  if (rtimer <= 90) {
    avgcpm = gcounter.get_cpm();
  } else if (rtimer <= 450) {
    avgcpm = gcounter.get_cpm5();
  } else {
    avgcpm = gcounter.get_cpm15();
  }
  char url[256];
  sprintf(url, RADMON_URI, _api_user, _api_key, avgcpm);

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
