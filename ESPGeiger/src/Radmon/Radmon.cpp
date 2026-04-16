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

Radmon radmon;
EG_REGISTER_MODULE(radmon)

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
  if (lastPing == 0) {
    ConfigManager &configManager = ConfigManager::getInstance();
    const char* _rtimer = configManager.getParamValueFromID("radmonTime");
    int rtimer = (_rtimer != NULL) ? atoi(_rtimer) : 0;
    if (rtimer == 0) {
      rtimer = RADMON_INTERVAL;
    }
    setInterval(rtimer);
    lastPing = stick_now + random(pingInterval / 1000) * 1000;
    return;
  }
  if (stick_now > lastPing && stick_now - lastPing >= (unsigned long)pingInterval)
  {
    lastPing = stick_now - (stick_now % 1000);
    Radmon::postMeasurement();
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
      // strstr() on c_str() avoids the temp String that indexOf("literal")
      // builds for each call (one small heap alloc per probe).
      String response = request->responseText();
      const char* r = response.c_str();
      if (strstr(r, "OK")) {
        Log::console(PSTR("Radmon: Upload OK"));
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
  }
}

void Radmon::postMeasurement() {

  ConfigManager &configManager = ConfigManager::getInstance();

  const char* _send = configManager.getParamValueFromID("radmonSend");
  if (_send == NULL || strcmp(_send, "N") == 0) {
    return;
  }

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Radmon: Testmode"));
    return;
  }

  const char* _api_user = configManager.getParamValueFromID("radmonUser");
  const char* _api_key = configManager.getParamValueFromID("radmonKey");
  if (_api_user == NULL || _api_key == NULL) {
    Log::console(PSTR("Radmon: Skipping upload, please set username and password"));
    return;
  }

  const char* _rtimer = configManager.getParamValueFromID("radmonTime");
  int rtimer = (_rtimer != NULL) ? atoi(_rtimer) : 0;
  if (rtimer == 0) {
    rtimer = RADMON_INTERVAL;
  }
  setInterval(rtimer);

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
  snprintf(url, sizeof(url), RADMON_URI, _api_user, _api_key, avgcpm);

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
      Log::console(PSTR("Radmon: Can't send request"));
    }
  }
}
#endif
