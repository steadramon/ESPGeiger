/*
  Webhook.cpp - Webhook class

  Copyright (C) 2025 @steadramon

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
#ifdef WEBHOOKOUT
#include "Webhook.h"
#include "../Logger/Logger.h"
#include "ArduinoJson.h"

Webhook::Webhook() {
}

void Webhook::s_tick(unsigned long stick_now)
{
  ConfigManager &configManager = ConfigManager::getInstance();
  pingInterval = atoi(configManager.getParamValueFromID("whTime"))*1000;
  if (stick_now - lastPing >= pingInterval)
  {
    if (lastPing == 0) {
      lastPing = random(30) * 1000;
      return;
    }

    lastPing = stick_now - (stick_now % 1000);
    Webhook::postMeasurement();
  }
}

void Webhook::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    if (request->responseHTTPcode() == 200)
    {
      String response = request->responseText();
      if (response.indexOf("OK") == 1) {
        Log::console(PSTR("Webhook: Upload OK"));
      } else {
        Log::console(PSTR("Webhook: Error - %s"), response.substring(0, 100));
      }
    } else {
      Log::console(PSTR("Webhook: Error %d - %s"), request->responseHTTPcode(), request->responseHTTPString().c_str());
    }
  }
}

const char* Webhook::cleanHTTP(const char* url) {
  if (strncmp(url, "http://", 7) == 0) {
    return url + 7; // Skip "http://"
  }
  if (strncmp(url, "https://", 8) == 0) {
    return url + 8; // Skip "https://"
  }  
  return url;
}

void Webhook::postMeasurement() {
#ifdef GEIGERTESTMODE
  Log::console(PSTR("Webhook: Testmode"));
  return;
#endif

  ConfigManager &configManager = ConfigManager::getInstance();
  const char* _send = configManager.getParamValueFromID("whSend");

  if ((_send == NULL) || (strcmp(_send, "N") == 0)) {
    return;
  }

  const char* whURL = configManager.getParamValueFromID("whURL");

  if (whURL == NULL) {
    return;
  }

  Log::console(PSTR("Webhook: Uploading latest data ..."));

  const char* key = configManager.getParamValueFromID("whKey");
  if (key == NULL) {
    key = "";
  }

  DynamicJsonDocument doc(1024);
  char buffer[1024];
  doc["id"] = configManager.getChipID();
  doc["key"] = key;
  doc["ut"] = configManager.getUptime();
  char fbuff[10];
  sprintf(fbuff, "%.2f", gcounter.get_cps());
  doc["cps"] = atof(fbuff);
  sprintf(fbuff, "%.2f", gcounter.get_cpmf());
  doc["cpm"] = atof(fbuff);
  sprintf(fbuff, "%.2f", gcounter.get_cpm5f());
  doc["cpm5"] = atof(fbuff);
  sprintf(fbuff, "%.2f", gcounter.get_cpm15f());
  doc["cpm15"] = atof(fbuff);
  sprintf(fbuff, "%.2f", gcounter.get_usv());
  doc["usv"] = atof(fbuff);
#ifdef ESPGEIGER_HW
  sprintf(fbuff, "%.2f", status.hvReading.get());
  doc["hv"] = atof(fbuff);
#endif
  doc["mem"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();

  serializeJson(doc, buffer);
  doc.clear();

  char url[256];
  const char* trimmedURL = cleanHTTP(whURL);

  snprintf(url, sizeof(url), "http://%s", trimmedURL);

  static bool requestOpenResult;
  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone)
  {
    requestOpenResult = request.open("POST", url);
    if (requestOpenResult)
    {
      status.led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), configManager.getUserAgent());
      request.setReqHeader(F("Accept"), F("application/json"));
      request.setReqHeader(F("Content-Type"), F("application/json"));
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(10);
      request.send(buffer);
      status.last_send = millis();
    }
    else
    {
      Serial.println(F("Can't send - bad request"));
    }
  }
}
#endif
