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
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Module/Registry.h"
#include "Corium/Measure/Measure.h"
#include "Counter/CounterMeasures.h"
#include "Counter/CounterUpload.h"
#include "Corium/Util/Words.h"
#include <EGEscape.h>
#include <EGJson.h>
#include <EGHttpStrings.h>

CORIUM_PSTR(K_URL, "url");
CORIUM_PSTR(K_KEY, "key");

Webhook webhook;
CORIUM_REGISTER_FEATURE(webhook, FEAT_WEBHOOK)

CORIUM_PSTR(WH_H_EN,  "Enable webhook POSTs");
CORIUM_PSTR(WH_L_URL, "URL");
CORIUM_PSTR(WH_H_URL, "Webhook endpoint (http only)");
// a url's chars less ' # [ ], escaped for the browser's v flag
CORIUM_PSTR(WH_P_URL, "https?://[A-Za-z0-9.:_~%?=&+,;!$*@\\(\\)\\/\\-]+");
CORIUM_PSTR(WH_L_KEY, "Key");
CORIUM_PSTR(WH_H_KEY, "Optional shared secret sent as \"key\"");
CORIUM_PSTR(WH_H_INT, "POST interval (sec)");


static const corium::Pref WEBHOOK_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND,     EG_L_EN,  WH_H_EN,  "0",  nullptr,  0, 0,    0,   corium::P_BOOL,   0},
  {K_URL,         WH_L_URL, WH_H_URL, "",   WH_P_URL, 0, 0,    CORIUM_PREFS_MAX_VALUE, corium::P_STRING, 0},
  {K_KEY,         WH_L_KEY, WH_H_KEY, "",   nullptr,  0, 0,    CORIUM_PREFS_MAX_VALUE, corium::P_STRING, corium::P_SENSITIVE},
  {EG_K_INTERVAL, EG_L_INT, WH_H_INT, STR(WEBHOOK_INTERVAL), nullptr,  WEBHOOK_INTERVAL_MIN, WEBHOOK_INTERVAL_MAX, 0, corium::P_UINT, 0},
};

static const corium::PrefGroup WEBHOOK_PREF_GROUP PROGMEM = {
  "webhook", "Webhook", 1,
  WEBHOOK_PREF_ITEMS,
  sizeof(WEBHOOK_PREF_ITEMS) / sizeof(WEBHOOK_PREF_ITEMS[0]),
  corium::P_CAT_UPLOAD, EG_K_SEND,
};

const corium::PrefGroup* Webhook::prefs_group() { return &WEBHOOK_PREF_GROUP; }

// === LEGACY IMPORT (remove in v1.1.0) ===
static const corium::LegacyAlias WEBHOOK_LEGACY[] PROGMEM = {
  {"whSend", "send"},
  {"whURL",  "url"},
  {"whKey",  "key"},
  {"whTime", "interval"},
  {},
};
corium::LegacyRows Webhook::legacy_aliases() { return WEBHOOK_LEGACY; }
// === END LEGACY IMPORT ===

Webhook::Webhook() {
  pingIntervalMs = (uint32_t)WEBHOOK_INTERVAL * 1000UL;
}

void Webhook::setInterval(int interval)
{
  pingInterval = set_interval_s(interval, WEBHOOK_INTERVAL_MIN, WEBHOOK_INTERVAL_MAX);
}

void Webhook::on_prefs_loaded() {
  int iv = (int)corium::Prefs::getUInt("webhook", EG_K_INTERVAL);
  if (iv > 0) setInterval(iv);
  set_enabled(corium::Prefs::getBool("webhook", EG_K_SEND));
}

bool Webhook::interpret(const char* r)
{
  if (strstr_P(r, W_OK_TEXT)) {
    Log::debug(PSTR("Webhook: Upload OK"));
    return true;
  }
  char esc[96];   // log only, cut is fine
  egesc_json(esc, sizeof(esc), r);
  Log::console(PSTR("Webhook: Error - %s"), esc);
  return false;
}

PGM_P Webhook::content_type() const { return HTTP_CT_JSON; }

const char* Webhook::cleanHTTP(const char* url) {
  if (strncmp_P(url, PSTR("http://"), 7) == 0) {
    return url + 7; // Skip "http://"
  }
  return url;
}

static constexpr size_t WEBHOOK_BUF_SIZE = 512;

static bool build_body(char* buffer, const char* key) {
  EGJson j(buffer, WEBHOOK_BUF_SIZE);
  j.map_begin();
  j.str(PSTR("id"), DeviceInfo::chipid());
  if (key[0] != '\0') j.str(PSTR("key"), key);
  corium::m_json_fields(EGM_OUT_WEBHOOK, j);
  j.map_end();
  return !j.overflow;
}

bool Webhook::prepare(char* url, size_t cap, const char** body) {
  const char* whURL = corium::Prefs::getString("webhook", K_URL);
  if (whURL[0] == '\0') return false;
  if (!upload_ready(log_tag())) return false;
  // never sent as plain http
  if (strncmp_P(whURL, PSTR("https://"), 8) == 0) {
    Log::console(PSTR("Webhook: https not supported, not sent"));
    note_publish(false);
    return false;
  }

  Log::debug(PSTR("Webhook: Uploading latest data ..."));

  const char* key = corium::Prefs::getString("webhook", K_KEY);

  const char* trimmedURL = cleanHTTP(whURL);
  if (!url_fits(snprintf_P(url, cap, PSTR("http://%s"), trimmedURL), cap)) return false;

  // post() frees it once sent
  char* buffer = (char*)malloc(WEBHOOK_BUF_SIZE);
  if (!buffer) {
    Log::console(PSTR("Webhook: malloc(%u) failed"), (unsigned)WEBHOOK_BUF_SIZE);
    return false;
  }
  if (!build_body(buffer, key)) {
    free(buffer);
    Log::console(PSTR("Webhook: body too long, not sent"));
    return false;
  }
  *body = buffer;
  return true;
}
#endif
