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
#include "Corium/Logger/Logger.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Measure/EGMeasure.h"
#include "Corium/Util/Words.h"
#include <EGEscape.h>
#include <EGJson.h>
#include <EGHttpStrings.h>

EG_PSTR(K_URL, "url");
EG_PSTR(K_KEY, "key");
EG_PSTR(M_WARM, "warm");

Webhook webhook;
EG_REGISTER_MODULE(webhook)

EG_PSTR(WH_H_EN,  "Enable webhook POSTs");
EG_PSTR(WH_L_URL, "URL");
EG_PSTR(WH_H_URL, "Webhook endpoint (http only)");
EG_PSTR(WH_P_URL, "https?://[A-Za-z0-9.:_%\\/\\-]+");
EG_PSTR(WH_L_KEY, "Key");
EG_PSTR(WH_H_KEY, "Optional shared secret sent as \"key\"");
EG_PSTR(WH_H_INT, "POST interval (sec)");


static const EGPref WEBHOOK_PREF_ITEMS[] PROGMEM = {
  {EG_K_SEND,     EG_L_EN,  WH_H_EN,  "0",  nullptr,  0, 0,    0,   EGP_BOOL,   0},
  {K_URL,         WH_L_URL, WH_H_URL, "",   WH_P_URL, 0, 0,    EGPREFS_MAX_VALUE, EGP_STRING, 0},
  {K_KEY,         WH_L_KEY, WH_H_KEY, "",   nullptr,  0, 0,    EGPREFS_MAX_VALUE, EGP_STRING, EGP_SENSITIVE},
  {EG_K_INTERVAL, EG_L_INT, WH_H_INT, STR(WEBHOOK_INTERVAL), nullptr,  WEBHOOK_INTERVAL_MIN, WEBHOOK_INTERVAL_MAX, 0, EGP_UINT, 0},
};

static const EGPrefGroup WEBHOOK_PREF_GROUP PROGMEM = {
  "webhook", "Webhook", 1,
  WEBHOOK_PREF_ITEMS,
  sizeof(WEBHOOK_PREF_ITEMS) / sizeof(WEBHOOK_PREF_ITEMS[0]),
  EGP_CAT_UPLOAD, EG_K_SEND,
};

const EGPrefGroup* Webhook::prefs_group() { return &WEBHOOK_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias WEBHOOK_LEGACY[] PROGMEM = {
  {"whSend", "send"},
  {"whURL",  "url"},
  {"whKey",  "key"},
  {"whTime", "interval"},
  {},
};
EGLegacyRows Webhook::legacy_aliases() { return WEBHOOK_LEGACY; }
// === END LEGACY IMPORT ===

Webhook::Webhook() {
  pingIntervalMs = (uint32_t)WEBHOOK_INTERVAL * 1000UL;
}

void Webhook::setInterval(int interval)
{
  if (interval < WEBHOOK_INTERVAL_MIN) interval = WEBHOOK_INTERVAL_MIN;
  if (interval > WEBHOOK_INTERVAL_MAX) interval = WEBHOOK_INTERVAL_MAX;
  pingInterval = interval;
  pingIntervalMs = (uint32_t)interval * 1000UL;
}

void Webhook::on_prefs_loaded() {
  int iv = (int)EGPrefs::getUInt("webhook", EG_K_INTERVAL);
  if (iv > 0) setInterval(iv);
  set_enabled(EGPrefs::getBool("webhook", EG_K_SEND));
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

void Webhook::add_headers(EGHttpClient* r)
{
  r->setReqHeader(F("Accept"), FPSTR(HTTP_CT_JSON));
  r->setReqHeader(FPSTR(HTTP_H_CTYPE), FPSTR(HTTP_CT_JSON));
}

const char* Webhook::cleanHTTP(const char* url) {
  if (strncmp_P(url, PSTR("http://"), 7) == 0) {
    return url + 7; // Skip "http://"
  }
  if (strncmp_P(url, PSTR("https://"), 8) == 0) {
    return url + 8; // Skip "https://"
  }
  return url;
}

static constexpr size_t WEBHOOK_BUF_SIZE = 512;

static bool build_body(char* buffer, const char* key) {
  EGJson j(buffer, WEBHOOK_BUF_SIZE);
  j.map_begin();
  j.str(PSTR("id"), DeviceInfo::chipid());
  if (key[0] != '\0') j.str(PSTR("key"), key);
  egm_json_fields(EGM_OUT_WEBHOOK, j);
  j.map_end();
  return !j.overflow;
}

bool Webhook::prepare(char* url, size_t cap, const char** body) {
  if (!egm_b(M_WARM)) return false;

  const char* whURL = EGPrefs::getString("webhook", K_URL);
  if (whURL[0] == '\0') return false;

  if (GEIGER_IS_TEST(GEIGER_TYPE)) {
    Log::console(PSTR("Webhook: Testmode"));
    return false;
  }

  Log::debug(PSTR("Webhook: Uploading latest data ..."));

  const char* key = EGPrefs::getString("webhook", K_KEY);

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
