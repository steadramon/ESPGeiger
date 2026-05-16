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
#include "../Util/StringUtil.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

extern uint8_t send_indicator;

Webhook webhook;
EG_REGISTER_MODULE(webhook)

EG_PSTR(WH_L_EN,  "Enable");
EG_PSTR(WH_H_EN,  "Enable webhook POSTs");
EG_PSTR(WH_L_URL, "URL");
EG_PSTR(WH_H_URL, "Webhook endpoint (http only)");
EG_PSTR(WH_P_URL, "https?://[A-Za-z0-9.:_%\\/\\-]+");
EG_PSTR(WH_L_KEY, "Key");
EG_PSTR(WH_H_KEY, "Optional shared secret sent as \"key\"");
EG_PSTR(WH_L_INT, "Interval");
EG_PSTR(WH_H_INT, "POST interval (sec)");

static const EGPref WEBHOOK_PREF_ITEMS[] = {
  {"send",     WH_L_EN,  WH_H_EN,  "0",  nullptr,  0, 0,    0,   EGP_BOOL,   0},
  {"url",      WH_L_URL, WH_H_URL, "",   WH_P_URL, 0, 0,    255, EGP_STRING, 0},
  {"key",      WH_L_KEY, WH_H_KEY, "",   nullptr,  0, 0,    255, EGP_STRING, EGP_SENSITIVE},
  {"interval", WH_L_INT, WH_H_INT, "60", nullptr,  WEBHOOK_INTERVAL_MIN, WEBHOOK_INTERVAL_MAX, 0, EGP_UINT, 0},
};

static const EGPrefGroup WEBHOOK_PREF_GROUP = {
  "webhook", "Webhook", 1,
  WEBHOOK_PREF_ITEMS,
  sizeof(WEBHOOK_PREF_ITEMS) / sizeof(WEBHOOK_PREF_ITEMS[0]),
};

const EGPrefGroup* Webhook::prefs_group() { return &WEBHOOK_PREF_GROUP; }

size_t Webhook::status_json(char* buf, size_t cap, unsigned long now) {
  if (!_send_enabled) return 0;
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

void Webhook::on_prefs_loaded() {
  int iv = (int)EGPrefs::getUInt("webhook", "interval");
  if (iv > 0) setInterval(iv);
  _send_enabled = EGPrefs::getBool("webhook", "send");
  EGModuleRegistry::set_loop_interval(this, _send_enabled ? 500 : 60000);
}

void Webhook::loop(unsigned long now)
{
  if (!_send_enabled) return;
  if (lastPing == 0) {
    lastPing = now - pingIntervalMs + random(pingIntervalMs);
  } else if ((now - lastPing) >= pingIntervalMs) {
    lastPing += pingIntervalMs;
    if ((now - lastPing) >= pingIntervalMs) lastPing = now;
    postMeasurement();
  }
  EGModuleRegistry::sleep_until(this, now, lastPing + pingIntervalMs);
}

void Webhook::httpRequestCb(void *optParm, AsyncHTTPRequest *request, int readyState)
{
  if (readyState == readyStateDone)
  {
    Webhook* self = static_cast<Webhook*>(optParm);
    self->last_ok = false;
    if (request->responseHTTPcode() == 200)
    {
      char r[128];
      size_t got = request->responseRead((uint8_t*)r, sizeof(r) - 1);
      r[got] = 0;
      if (strstr(r, "OK")) {
        Log::debug(PSTR("Webhook: Upload OK"));
        self->last_ok = true;
      } else {
        Log::console(PSTR("Webhook: Error - %s"), r);
      }
    } else {
      Log::console(PSTR("Webhook: Error %d - %s"), request->responseHTTPcode(), request->responseHTTPString().c_str());
    }
    self->note_result(self->last_ok);
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
  if (!gcounter.is_warm()) return;

  const char* whURL = EGPrefs::getString("webhook", "url");
  if (whURL[0] == '\0') return;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Webhook: Testmode"));
    return;
  }

  Log::debug(PSTR("Webhook: Uploading latest data ..."));

  const char* key = EGPrefs::getString("webhook", "key");

  // Lazy-allocate on first send - saves 512 B BSS for users who never
  // configure a webhook URL. Once allocated it persists for the lifetime
  // of the firmware (cheaper than malloc/free per call, and the async
  // request needs the buffer to outlive postMeasurement anyway).
  static constexpr size_t WEBHOOK_BUF_SIZE = 512;
  static char* buffer = nullptr;
  if (!buffer) {
    buffer = (char*)malloc(WEBHOOK_BUF_SIZE);
    if (!buffer) {
      Log::console(PSTR("Webhook: malloc(%u) failed"), (unsigned)WEBHOOK_BUF_SIZE);
      return;
    }
  }
  char b_cps[12], b_cpm[12], b_cpm5[12], b_cpm15[12], b_usv[12];
  format_f(b_cps,   sizeof(b_cps),   gcounter.get_cps());
  format_f(b_cpm,   sizeof(b_cpm),   gcounter.get_cpmf());
  format_f(b_cpm5,  sizeof(b_cpm5),  gcounter.get_cpm5f());
  format_f(b_cpm15, sizeof(b_cpm15), gcounter.get_cpm15f());
  format_f(b_usv,   sizeof(b_usv),   gcounter.get_usv());

  size_t pos = 0;
  int n;
  n = snprintf_P(buffer, WEBHOOK_BUF_SIZE,
    PSTR("{\"id\":\"%s\""), DeviceInfo::chipid());
  advance_pos(pos, n, WEBHOOK_BUF_SIZE);
  if (key[0] != '\0') {
    n = snprintf_P(buffer + pos, WEBHOOK_BUF_SIZE - pos, PSTR(",\"key\":\"%s\""), key);
    advance_pos(pos, n, WEBHOOK_BUF_SIZE);
  }
  n = snprintf_P(buffer + pos, WEBHOOK_BUF_SIZE - pos,
    PSTR(",\"ut\":%lu,\"cps\":%s,\"cpm\":%s,\"cpm5\":%s,\"cpm15\":%s,\"usv\":%s"),
    DeviceInfo::uptime(), b_cps, b_cpm, b_cpm5, b_cpm15, b_usv);
  advance_pos(pos, n, WEBHOOK_BUF_SIZE);
#ifdef ESPG_HV_ADC
  char b_hv[12];
  format_f(b_hv, sizeof(b_hv), hv.hvReading.get());
  n = snprintf_P(buffer + pos, WEBHOOK_BUF_SIZE - pos, PSTR(",\"hv\":%s"), b_hv);
  advance_pos(pos, n, WEBHOOK_BUF_SIZE);
#endif
  n = snprintf_P(buffer + pos, WEBHOOK_BUF_SIZE - pos,
    PSTR(",\"tc\":%u,\"mem\":%u,\"rssi\":%d}"),
    gcounter.total_clicks, DeviceInfo::freeHeap(), (int)Wifi::rssi);
  advance_pos(pos, n, WEBHOOK_BUF_SIZE);
  buffer[pos] = '\0';

  char url[256];
  const char* trimmedURL = cleanHTTP(whURL);

  snprintf_P(url, sizeof(url), PSTR("http://%s"), trimmedURL);

  if (!request) request = new AsyncHTTPRequest();
  if (!request) { Log::console(PSTR("Webhook: alloc failed")); return; }

  if (request->readyState() == readyStateUnsent || request->readyState() == readyStateDone)
  {
    if (request->open("POST", url))
    {
      led.Blink(500, 500);
      request->setReqHeader(F("User-Agent"), DeviceInfo::useragent());
      request->setReqHeader(F("Accept"), F("application/json"));
      request->setReqHeader(F("Content-Type"), F("application/json"));
      request->onReadyStateChange(httpRequestCb, this);
      request->setTimeout(10);
      request->send(buffer);
      note_attempt();
      send_indicator = 2;
    }
    else
    {
      Serial.println(F("Can't send - bad request"));
    }
  }
}
#endif
