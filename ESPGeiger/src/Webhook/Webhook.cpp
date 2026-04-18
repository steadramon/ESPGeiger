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
#include "../Util/Wifi.h"
#ifdef ESPGEIGER_HW
#include "../ESPGHW/ESPGHW.h"
#endif

Webhook webhook;
EG_REGISTER_MODULE(webhook)

static const EGPref WEBHOOK_PREF_ITEMS[] = {
  {"send",     "Enable",      "Enable webhook POSTs",            "0",  nullptr, 0, 0,    0,  EGP_BOOL,   0},
  {"url",      "URL",         "Webhook endpoint (http only)",    "",   nullptr, 0, 0, 255, EGP_STRING, 0},
  {"key",      "Key",         "Optional shared secret sent as \"key\"", "", nullptr, 0, 0, 255, EGP_STRING, EGP_SENSITIVE},
  {"interval", "Interval",    "POST interval (sec)",             "60", nullptr, WEBHOOK_INTERVAL_MIN, WEBHOOK_INTERVAL_MAX, 0, EGP_UINT, 0},
};

static const EGPrefGroup WEBHOOK_PREF_GROUP = {
  "webhook", "Webhook", 1,
  WEBHOOK_PREF_ITEMS,
  sizeof(WEBHOOK_PREF_ITEMS) / sizeof(WEBHOOK_PREF_ITEMS[0]),
};

const EGPrefGroup* Webhook::prefs_group() { return &WEBHOOK_PREF_GROUP; }

size_t Webhook::status_json(char* buf, size_t cap, unsigned long now) {
  if (!EGPrefs::getBool("webhook", "send")) return 0;
  return write_status_json(buf, cap, "webhook", last_ok, last_attempt_ms, now);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias WEBHOOK_LEGACY[] = {
  {"whSend", "send"},
  {"whURL",  "url"},
  {"whKey",  "key"},
  {"whTime", "interval"},
  {nullptr, nullptr},
};
const EGLegacyAlias* Webhook::legacy_aliases() { return WEBHOOK_LEGACY; }
// === END LEGACY IMPORT ===

Webhook::Webhook() {
}

void Webhook::setInterval(int interval)
{
  if (interval < WEBHOOK_INTERVAL_MIN) interval = WEBHOOK_INTERVAL_MIN;
  if (interval > WEBHOOK_INTERVAL_MAX) interval = WEBHOOK_INTERVAL_MAX;
  pingInterval = interval;
  pingIntervalMs = (uint32_t)interval * 1000UL;
}

void Webhook::loop(unsigned long now)
{
  if (lastPing == 0) {
    int iv = (int)EGPrefs::getUInt("webhook", "interval");
    if (iv > 0) setInterval(iv);
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
  if (!EGPrefs::getBool("webhook", "send")) return;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Webhook: Testmode"));
    return;
  }

  const char* whURL = EGPrefs::getString("webhook", "url");
  if (whURL[0] == '\0') return;

  int iv = (int)EGPrefs::getUInt("webhook", "interval");
  if (iv > 0) setInterval(iv);

  Log::console(PSTR("Webhook: Uploading latest data ..."));

  const char* key = EGPrefs::getString("webhook", "key");

  static char buffer[512];
  char b_cps[12], b_cpm[12], b_cpm5[12], b_cpm15[12], b_usv[12];
  format_f(b_cps,   sizeof(b_cps),   gcounter.get_cps());
  format_f(b_cpm,   sizeof(b_cpm),   gcounter.get_cpmf());
  format_f(b_cpm5,  sizeof(b_cpm5),  gcounter.get_cpm5f());
  format_f(b_cpm15, sizeof(b_cpm15), gcounter.get_cpm15f());
  format_f(b_usv,   sizeof(b_usv),   gcounter.get_usv());

  int pos = snprintf_P(buffer, sizeof(buffer),
    PSTR("{\"id\":\"%s\""), DeviceInfo::chipid());
  if (key[0] != '\0') {
    pos += snprintf_P(buffer + pos, sizeof(buffer) - pos, PSTR(",\"key\":\"%s\""), key);
  }
  pos += snprintf_P(buffer + pos, sizeof(buffer) - pos,
    PSTR(",\"ut\":%lu,\"cps\":%s,\"cpm\":%s,\"cpm5\":%s,\"cpm15\":%s,\"usv\":%s"),
    DeviceInfo::uptime(), b_cps, b_cpm, b_cpm5, b_cpm15, b_usv);
#ifdef ESPGEIGER_HW
  char b_hv[12];
  format_f(b_hv, sizeof(b_hv), hardware.hvReading.get());
  pos += snprintf_P(buffer + pos, sizeof(buffer) - pos, PSTR(",\"hv\":%s"), b_hv);
#endif
  pos += snprintf_P(buffer + pos, sizeof(buffer) - pos,
    PSTR(",\"tc\":%u,\"mem\":%u,\"rssi\":%d}"),
    gcounter.total_clicks, ESP.getFreeHeap(), (int)Wifi::rssi);

  char url[256];
  const char* trimmedURL = cleanHTTP(whURL);

  snprintf(url, sizeof(url), "http://%s", trimmedURL);

  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone)
  {
    if (request.open("POST", url))
    {
      status.led.Blink(500, 500);
      request.setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request.setReqHeader(F("Accept"), F("application/json"));
      request.setReqHeader(F("Content-Type"), F("application/json"));
      request.onReadyStateChange(httpRequestCb, this);
      request.setTimeout(10);
      request.send(buffer);
      status.send_indicator = 2;
    }
    else
    {
      Serial.println(F("Can't send - bad request"));
    }
  }
}
#endif
