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

Thingspeak::Thingspeak() {
}

void Thingspeak::s_tick(unsigned long stick_now)
{
  if (lastPing == 0) {
    lastPing = stick_now + random(pingInterval / 1000) * 1000;
    return;
  }
  if (stick_now > lastPing && stick_now - lastPing >= (unsigned long)pingInterval)
  {
    lastPing = stick_now - (stick_now % 1000);
    Thingspeak::postMeasurement();
  }
}

void Thingspeak::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    if (request->responseHTTPcode() == 200)
    {
      String response = request->responseText();
      if (response != "0") {
        Log::console(PSTR("Thingspeak: Upload OK"));
      } else {
        Log::console(PSTR("Thingspeak: Error!"));
      }
    } else {
      Log::console(PSTR("Thingspeak: Error - %s"), request->responseHTTPString().c_str());
    }
  }
}

void Thingspeak::postMeasurement() {

  ConfigManager &configManager = ConfigManager::getInstance();
  const char* _send = configManager.getParamValueFromID("tsSend");

  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    return;
  }
  
  const char* _ts_channel_key = configManager.getParamValueFromID("tsChannelKey");

  if (_ts_channel_key == NULL) {
    return;
  }

#ifdef GEIGERTESTMODE
  Log::console(PSTR("Thingspeak: Testmode"));
  return;
#endif

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
      request.setReqHeader(F("User-Agent"), configManager.getUserAgent());
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(5);
      request.send();
      status.last_send = millis();
    }
    else
    {
      Log::console(PSTR("Thingspeak: Can't send request"));
    }
  }
}
#endif
