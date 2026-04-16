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
#include "../Module/EGModuleRegistry.h"
#include "ArduinoJson.h"

Webhook webhook;
EG_REGISTER_MODULE(webhook)

Webhook::Webhook() {
}

void Webhook::setInterval(int interval)
{
  if (interval < WEBHOOK_INTERVAL_MIN) interval = WEBHOOK_INTERVAL_MIN;
  if (interval > WEBHOOK_INTERVAL_MAX) interval = WEBHOOK_INTERVAL_MAX;
  pingInterval = interval * 1000;
}

void Webhook::s_tick(unsigned long stick_now)
{
  if (lastPing == 0) {
    ConfigManager &configManager = ConfigManager::getInstance();
    const char* _whTime = configManager.getParamValueFromID("whTime");
    if (_whTime != NULL) {
      setInterval(atoi(_whTime));
    }
    lastPing = stick_now + random(pingInterval / 1000) * 1000;
    return;
  }
  if (stick_now > lastPing && stick_now - lastPing >= (unsigned long)pingInterval)
  {
    lastPing = stick_now - (stick_now % 1000);
    Webhook::postMeasurement();
  }
}

void Webhook::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    Webhook* self = static_cast<Webhook*>(optParm);
    self->last_attempt_ms = millis();
    self->last_ok = false;
    if (request->responseHTTPcode() == 200)
    {
      String response = request->responseText();
      if (strstr(response.c_str(), "OK")) {
        Log::console(PSTR("Webhook: Upload OK"));
        self->last_ok = true;
      } else {
        Log::console(PSTR("Webhook: Error - %s"), response.substring(0, 100).c_str());
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
  ConfigManager &configManager = ConfigManager::getInstance();
  const char* _send = configManager.getParamValueFromID("whSend");
  if (_send == NULL || strcmp(_send, "N") == 0) {
    return;
  }

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Webhook: Testmode"));
    return;
  }

  const char* whURL = configManager.getParamValueFromID("whURL");
  if (whURL == NULL) {
    return;
  }

  const char* _whTime = configManager.getParamValueFromID("whTime");
  if (_whTime != NULL) {
    setInterval(atoi(_whTime));
  }

  Log::console(PSTR("Webhook: Uploading latest data ..."));

  const char* key = configManager.getParamValueFromID("whKey");

  DynamicJsonDocument doc(512);
  static char buffer[512];
  // Formatted float buffers — each must stay alive until serializeJson()
  // runs because serialized() stores a pointer, not a copy. Stack-local
  // avoids heap churn from the previous `String(float, 2)` pattern.
  char b_cps[12], b_cpm[12], b_cpm5[12], b_cpm15[12], b_usv[12];
  format_f(b_cps,   sizeof(b_cps),   gcounter.get_cps());
  format_f(b_cpm,   sizeof(b_cpm),   gcounter.get_cpmf());
  format_f(b_cpm5,  sizeof(b_cpm5),  gcounter.get_cpm5f());
  format_f(b_cpm15, sizeof(b_cpm15), gcounter.get_cpm15f());
  format_f(b_usv,   sizeof(b_usv),   gcounter.get_usv());
#ifdef ESPGEIGER_HW
  char b_hv[12];
  format_f(b_hv, sizeof(b_hv), status.hvReading.get());
#endif

  doc["id"] = configManager.getChipID();
  if (key != NULL && key[0] != '\0') {
    doc["key"] = key;
  }
  doc["ut"] = configManager.getUptime();
  doc["cps"]   = serialized(b_cps);
  doc["cpm"]   = serialized(b_cpm);
  doc["cpm5"]  = serialized(b_cpm5);
  doc["cpm15"] = serialized(b_cpm15);
  doc["usv"]   = serialized(b_usv);
#ifdef ESPGEIGER_HW
  doc["hv"]    = serialized(b_hv);
#endif
  doc["tc"] = gcounter.total_clicks;
  doc["mem"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();

  serializeJson(doc, buffer);
  doc.clear();

  char url[256];
  const char* trimmedURL = cleanHTTP(whURL);

  snprintf(url, sizeof(url), "http://%s", trimmedURL);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone)
  {
    if (request.open("POST", url))
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
