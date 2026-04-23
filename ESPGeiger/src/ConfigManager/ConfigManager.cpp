/*
  ConfigManager.cpp - Config Manager class

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
#include "ConfigManager.h"
#include "logos.h"
#include "html.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "ArduinoJson.h"
#include "../Prefs/EGPrefs.h"
#include <FS.h>
#include <LittleFS.h>
#include "../NTP/timezones.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../Util/TickProfile.h"
#include "../SerialOut/SerialOut.h"
#include "../Util/BootHooks.h"
#include "../Util/StringUtil.h"
#ifdef ESP32
#include <esp_system.h>
#endif

#define _STR(x) #x
#define STR(x) _STR(x)



ConfigManager::ConfigManager() : WiFiManager(){

#ifdef ESP8266
  uint32_t tchipId = ESP.getChipId();
#elif defined(ESP32)
  uint32_t tchipId = 0;
  for (int i=0; i<17; i=i+8) {
   tchipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
#endif
  String hexId = String(tchipId & 0xFFFFFF, HEX);
  strncpy(chipId, hexId.c_str(), sizeof(chipId) - 1);
  chipId[sizeof(chipId) - 1] = '\0';

  snprintf_P (hostName, sizeof(hostName), PSTR("%S-%S"), THING_NAME, chipId);
  snprintf_P(userAgent, sizeof(userAgent), PSTR("%S/%S (%S; %S; %S; %S)"),
             THING_NAME, RELEASE_VERSION, GIT_VERSION,
             BUILD_ENV, DeviceInfo::chipmodel(), chipId);
  strncpy(macAddr, WiFi.macAddress().c_str(), sizeof(macAddr) - 1);
  macAddr[sizeof(macAddr) - 1] = '\0';

  DeviceInfo::init(hostName, chipId, userAgent, macAddr);
  setHostname(hostName);
  setTitle(THING_NAME);
  setCustomHeadElement(faviconHead);
}

void ConfigManager::updatePortalMessage()
{
  if (!portalMenuHtml) {
    portalMenuHtml = (char*)malloc(PORTAL_MENU_HTML_SIZE);
    if (!portalMenuHtml) return;  // OOM — portal just renders without the banner
  }
  snprintf_P(portalMenuHtml, PORTAL_MENU_HTML_SIZE,
    PSTR("<div style='padding:.5em;background:#eef6ff;border:1px solid #89b'>"
         "After WiFi setup, open <b>http://%s.local/</b> "
         "(or find the device IP on your router)</div>"),
    hostName);
  setCustomMenuHTML(portalMenuHtml);
}
ParsedTime ConfigManager::parseTime(const char* timeStr) {
    ParsedTime pt;
    pt.hour = -1;
    pt.minute = -1;
    pt.isValid = false;

    if (timeStr == NULL || strlen(timeStr) != 5 || timeStr[2] != ':') {
        return pt; // Invalid format
    }

    // Create a mutable copy of the string as strtok modifies it
    char tempStr[6]; // HH:MM\0
    strncpy(tempStr, timeStr, sizeof(tempStr) - 1);
    tempStr[sizeof(tempStr) - 1] = '\0'; // Ensure null-termination

    char* hourStr = strtok(tempStr, ":");
    char* minStr = strtok(NULL, ":");

    if (hourStr == NULL || minStr == NULL) {
        return pt; // Parsing failed
    }

    int h = atoi(hourStr);
    int m = atoi(minStr);

    // Basic validation for hours and minutes
    if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        pt.hour = h;
        pt.minute = m;
        pt.isValid = true;
    }

    return pt;
}


bool ConfigManager::hasWiFiCreds()
{
#ifdef ESP32
  WiFi.mode(WIFI_STA);  // esp_wifi_get_config needs the stack up to read persistent creds
#endif
  return WiFiManager::getWiFiIsSaved();
}

bool ConfigManager::autoConnect()
{
  WiFiManager::setConnectTimeout(10);
  WiFiManager::setAPClientCheck(true);
  WiFiManager::setWiFiAutoReconnect(true);
  WiFiManager::setConfigPortalTimeout(300);
  static const char* portalMenu[] = {"custom","wifi","info","exit","sep","update"};
  setMenu(portalMenu, sizeof(portalMenu) / sizeof(portalMenu[0]));
  if (hasWiFiCreds()) {
    WiFiManager::setEnableConfigPortal(false);
    // No updatePortalMessage() — portal won't render; skip the 192 B alloc.
  } else {
    Log::console(PSTR("Config: No WiFi saved - captive portal on AP '%s' (300s)"), hostName);
    updatePortalMessage();
  }
  bool result = WiFiManager::autoConnect(hostName);

  if (result) {
    Log::console(PSTR("WiFi: IP: %s"), WiFi.localIP().toString().c_str());
    return result;
  }

  uint8_t connection_result = WiFiManager::getLastConxResult();
  if (connection_result == WL_STATION_WRONG_PASSWORD) {
    Log::console(PSTR("Config: WiFi password incorrect"));
    WiFiManager::setEnableConfigPortal(true);
    WiFiManager::setConfigPortalTimeout(300);
    Log::console(PSTR("Config: Entering setup for 300s"));
    BootHooks::displaySetupWifi(hostName);
    result = WiFiManager::autoConnect(hostName);
    if (!result) {
      Log::console(PSTR("WiFi password incorrect ... Restarting ... "));
      delay(1000);
      ESP.restart();
    }
    return result;
  }
  if (connection_result == WL_CONNECT_FAILED) {
      Log::console(PSTR("Connection FAILED ... Restarting ... "));
      delay(1000);
      ESP.restart();
  }

  if (hasWiFiCreds()) {
    WiFiManager::setEnableConfigPortal(true);
    WiFiManager::setConfigPortalTimeout(90);
    Log::console(PSTR("Config: Entering setup for 90s"));
    BootHooks::displaySetupWifi(hostName);
    result = WiFiManager::autoConnect(hostName);
  }

  return result;
}

void ConfigManager::startWebPortal()
{
  WiFiManager::setWebServerCallback(std::bind(&ConfigManager::bindServerCallback, this));
  static const char* webPortalMenu[] = {"wifi","info","exit","sep","update"};
  setMenu(webPortalMenu, sizeof(webPortalMenu) / sizeof(webPortalMenu[0]));
  WiFiManager::startWebPortal();
}

// Render a PROGMEM template into a stack buffer with token substitution, then send.
struct TemplateSub { const char* token; const char* value; };

static bool replace_token(char* buf, size_t bufsz, const char* token, const char* value) {
  char* pos = strstr(buf, token);
  if (!pos) return false;
  size_t tlen = strlen(token);
  size_t vlen = strlen(value);
  size_t tail = strlen(pos + tlen);
  if ((size_t)(pos - buf) + vlen + tail >= bufsz) return false;
  memmove(pos + vlen, pos + tlen, tail + 1);
  memcpy(pos, value, vlen);
  return true;
}

template <typename WS>
static void sendTemplate(WS* s, const char* pgm, const TemplateSub* subs, size_t count) {
  char buf[384];
  strncpy_P(buf, pgm, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  for (size_t i = 0; i < count; i++) {
    replace_token(buf, sizeof(buf), subs[i].token, subs[i].value);
  }
  size_t len = strlen(buf);
  if (len > 0) s->sendContent(buf, len);
}

// Convenience macro — deduces array count automatically
#define SEND_TEMPLATE(s, pgm, subs) sendTemplate(s, pgm, subs, sizeof(subs)/sizeof(subs[0]))

void ConfigManager::beginChunkedPage(const char* contentType) {
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  if (contentType)
    server->send(200, contentType, "");
  else
    server->send(200, FPSTR(HTTP_HEAD_CT), "");
}

void ConfigManager::sendPageHead(const char* title) {
  auto* s = server.get();
  { TemplateSub subs[] = {{"{v}", title}};
    SEND_TEMPLATE(s, HTTP_HEAD_START, subs); }
  s->sendContent(FPSTR(HTTP_STYLE));
  s->sendContent(FPSTR(faviconHead));
  s->sendContent(FPSTR(HTTP_HEAD_END));
}

void ConfigManager::endChunkedPage() {
  server->sendContent(FPSTR(HTTP_END));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleRoot() {
  handleRequest();
  beginChunkedPage();
  server->client().flush();
  sendPageHead(hostName);
  auto* s = server.get();

  char ip[16];
  strncpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip) - 1);
  ip[sizeof(ip) - 1] = '\0';

  char heading[64];
  snprintf_P(heading, sizeof(heading), PSTR("%s - %s"), hostName, ip);
  const char* tname =
#ifdef ESPGEIGER_HW
    "ESPGeiger-HW";
#elif defined(ESPGEIGER_LT)
    "ESPGeiger-Log";
#else
    THING_NAME;
#endif
  { TemplateSub subs[] = {{"{t}", tname}, {"{v}", heading}};
    SEND_TEMPLATE(s, HTTP_ROOT_MAIN, subs); }
  s->sendContent(F("<form action='/status' method='get'><button>Status</button></form><br/>"));
  s->sendContent(F("<form action='/hist' method='get'><button>History</button></form><br/>"));
  s->sendContent(F("<form action='/param' method='get'><button>Config</button></form><br/>"));
#ifdef ESPGEIGER_HW
  s->sendContent(F("<form action='/hv' method='get'><button>HV Config</button></form><br/>"));
#endif
  s->sendContent(F("<form action='/ntp' method='get'><button>NTP Config</button></form><br/>"));
  s->sendContent(FPSTR(HTTP_PORTAL_MENU[2]));
  s->sendContent(FPSTR(HTTP_PORTAL_MENU[8]));
  s->sendContent(F("<form action='/restart' method='get'><button>Reboot</button></form><br/>"));
  char ssid[33];
  strncpy(ssid, WiFiManager::getWiFiSSID().c_str(), sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  { TemplateSub subs[] = {{"{i}", ip}, {"{v}", ssid}};
    SEND_TEMPLATE(s, HTTP_STATUS_ON, subs); }
  endChunkedPage();
}

void ConfigManager::handleJSReturn()
{
  beginChunkedPage(HTTP_HEAD_CTJS);
  server->sendContent(FPSTR(picographJS));
  server->sendContent(FPSTR(statusJS));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleJsonReturn()
{
  char jsonBuffer[320] = "";

  const char* ratioChar = EGPrefs::getString("sys", "ratio");
  char c[16], s[16], c5[16], c15[16], cs[16];
  format_f(c, sizeof(c), gcounter.get_cpmf());
  format_f(s, sizeof(s), gcounter.get_usv());
  format_f(c5, sizeof(c5), gcounter.get_cpm5f());
  format_f(c15, sizeof(c15), gcounter.get_cpm15f());
  format_f(cs, sizeof(cs), gcounter.get_cps());
  size_t pos = 0;
  int n;
  n = snprintf_P (
    jsonBuffer,
    sizeof(jsonBuffer),
    PSTR("{\"ut\":%lu,\"c\":%s,\"s\":%s,\"c5\":%s,\"c15\":%s,\"cs\":%s,\"r\":%s,\"tc\":%u,\"mem\":%u,\"rssi\":%d"),
    DeviceInfo::uptime(),
    c, s, c5, c15, cs,
    ratioChar,
    gcounter.total_clicks,
    ESP.getFreeHeap(),
    (int)Wifi::rssi
  );
  advance_pos(pos, n, sizeof(jsonBuffer));
#ifdef ESPGEIGER_HW
  char hv[16];
  format_f(hv, sizeof(hv), hardware.hvReading.get());
  n = snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos, PSTR(",\"hv\":%s"), hv);
  advance_pos(pos, n, sizeof(jsonBuffer));
#endif
  n = snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
    PSTR(",\"tick\":%u,\"t_max\":%u,\"lps\":%u"),
    TickProfile::tick_us, TickProfile::tick_max_us, TickProfile::lps);
  advance_pos(pos, n, sizeof(jsonBuffer));
  n = snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos, PSTR("}"));
  advance_pos(pos, n, sizeof(jsonBuffer));
  jsonBuffer[pos] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer );
}

void ConfigManager::handleGeigerLog() {
  handleRequest();
  char jsonBuffer[128] = "";
  const char* ratioChar = EGPrefs::getString("sys", "ratio");
  char cpm[16], cps[16];
  format_f(cpm, sizeof(cpm), gcounter.get_cpmf());
  format_f(cps, sizeof(cps), gcounter.get_cps());
  snprintf_P (
    jsonBuffer,
    sizeof(jsonBuffer),
    PSTR("%s, %s, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan"),
    cpm, cps
  );
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CT), jsonBuffer );
}

#ifdef SERIALOUT
void ConfigManager::handleSerialOut() {
  handleRequest();
  if (serialout.interval() == 0) {
    serialout.setInterval(1);
    Log::setSerialLogLevel(false);
  } else {
    serialout.setInterval(0);
    Log::setSerialLogLevel(true);
  }
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CT), "OK" );
}
#endif

void ConfigManager::handleAbout()
{
  handleRequest();
  char jsonBuffer[1024] = "";
  size_t pos = 0;

  uint8_t macb[6];
  WiFi.macAddress(macb);
  char mac_str[13];
  snprintf_P(mac_str, sizeof(mac_str), PSTR("%02X%02X%02X%02X%02X%02X"),
    macb[0], macb[1], macb[2], macb[3], macb[4], macb[5]);

  int n;
  n = snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
    PSTR("{\"ver\":\"%s\",\"git\":\"%s\",\"env\":\"%s\","
         "\"date\":\"%s %s\",\"chip\":\"%s\",\"mac\":\"%s\","
         "\"host\":\"%s\",\"g_mod\":\"%s\","
         "\"g_type\":\"%s\",\"g_test\":%s,\"g_pcnt\":%s,"
         "\"modules\":["),
    RELEASE_VERSION, GIT_VERSION, BUILD_ENV,
    __DATE__, __TIME__,
    DeviceInfo::chipmodel(),
    mac_str,
    hostName,
    DeviceInfo::geigermodel(),
    GEIGER_IS_PULSE(GEIGER_TYPE)  ? "pulse" :
    GEIGER_IS_SERIAL(GEIGER_TYPE) ? "serial" : "none",
    GEIGER_IS_TEST(GEIGER_TYPE)   ? "true" : "false",
    gcounter.has_pcnt()           ? "true" : "false");
  advance_pos(pos, n, sizeof(jsonBuffer));

  uint8_t count = EGModuleRegistry::count();
  for (uint8_t i = 0; i < count; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (m == nullptr) continue;
    n = snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos,
      PSTR("%s\"%s\""), (i > 0 ? "," : ""), m->name());
    advance_pos(pos, n, sizeof(jsonBuffer));
  }
  n = snprintf_P(jsonBuffer + pos, sizeof(jsonBuffer) - pos, PSTR("]}"));
  advance_pos(pos, n, sizeof(jsonBuffer));
  jsonBuffer[pos] = '\0';
  ConfigManager::server.get()->send(200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer);
}

void ConfigManager::handleOutputsJson()
{
  handleRequest();
  char buf[512];
  size_t pos = 0;
  buf[pos++] = '{';
  pos += EGModuleRegistry::collect_status_json(buf + pos, sizeof(buf) - pos - 2, millis());
  buf[pos++] = '}';
  buf[pos] = '\0';
  ConfigManager::server.get()->send(200, FPSTR(HTTP_HEAD_CTJSON), buf);
}

// Append src to dst[pos..cap], HTML-escaping. Returns new pos. Callers keep
// a margin; stops before write would overflow rather than truncating mid-entity.
static size_t append_escaped(char* dst, size_t cap, size_t pos, const char* src) {
  while (*src && pos + 6 < cap) {
    char c = *src++;
    const char* rep = nullptr;
    switch (c) {
      case '<':  rep = "&lt;";   break;
      case '>':  rep = "&gt;";   break;
      case '&':  rep = "&amp;";  break;
      case '"':  rep = "&quot;"; break;
      case '\'': rep = "&#39;";  break;
    }
    if (rep) { size_t n = strlen(rep); memcpy(dst + pos, rep, n); pos += n; }
    else     { dst[pos++] = c; }
  }
  return pos;
}

template <typename WS>
static inline void send_chunk(WS* s, const char* buf, size_t len) {
  if (len > 0) s->sendContent(buf, len);
}


// memcpy a compile-time string literal into buf[pos..], advancing pos.
#define APPEND_LIT(buf, pos, cap, lit) do {                      \
  size_t _n = sizeof(lit) - 1;                                   \
  if ((pos) + _n < (cap)) { memcpy((buf)+(pos), lit, _n); (pos) += _n; } \
} while (0)


void ConfigManager::handleEGPrefs() {
  auto* s = ConfigManager::server.get();
  bool did_save = false;

  if (s->method() == HTTP_POST) {
    char name[64];
    String lookup;  lookup.reserve(64);
    String arg_val; arg_val.reserve(64);
    for (size_t gi = 0; gi < EGPrefs::group_count(); gi++) {
      const EGPrefGroup* g = EGPrefs::group_at(gi);
      if (!g) continue;
      for (size_t j = 0; j < g->count; j++) {
        const EGPref& p = g->prefs[j];
        if (p.flags & (EGP_HIDDEN | EGP_READONLY)) continue;
        snprintf_P(name, sizeof(name), PSTR("%s.%s"), g->module_id, p.id);
        lookup = name;
        char one[2] = {0, 0};
        const char* v;
        if (p.type == EGP_BOOL) {
          one[0] = s->hasArg(lookup) ? '1' : '0';
          v = one;
        } else {
          if (!s->hasArg(lookup)) continue;
          arg_val = s->arg(lookup);
          v = arg_val.c_str();
          if ((p.flags & EGP_SENSITIVE) && v[0] == '\0') continue;
        }
        EGPrefs::put(g->module_id, p.id, v);
      }
    }
    EGPrefs::commit();
    did_save = true;
    if (EGPrefs::restart_pending()) {
      s->sendHeader(F("Location"), RESTART_URL);
      s->send(302);
      return;
    }
  }

  beginChunkedPage();
  sendPageHead("Config");
  s->sendContent(F("<style>details{border:1px solid #eee;padding:8px;margin:8px 0}summary{font-weight:bold;padding:4px 0;cursor:pointer}label{display:inline-block;margin-top:10px}small{display:block;color:#666;margin-top:2px}hr{border:0;border-top:1px solid #ddd;margin:16px 0}@media(min-width:520px){details{min-width:500px}}</style>"));

  char buf[512];
  int n;

  n = snprintf_P(buf, sizeof(buf),
               PSTR("<h1>ESPGeiger Config</h1>%S<form method='post' action='/param'>"),
               did_save ? (PGM_P)PSTR("<p style='color:green'>Saved</p>") : (PGM_P)PSTR(""));
  send_chunk(s, buf, (n > 0) ? (size_t)n : 0);

  // Render groups sorted by EGModule::display_order (stable — preserves
  // registration order within the same bucket).
  uint8_t gc = (uint8_t)EGPrefs::group_count();
  uint8_t order[16];
  if (gc > sizeof(order)) gc = sizeof(order);
  for (uint8_t i = 0; i < gc; i++) order[i] = i;
  for (uint8_t i = 1; i < gc; i++) {
    uint8_t cur = order[i];
    EGModule* m_cur = EGPrefs::module_at(cur);
    uint8_t o_cur = m_cur ? m_cur->display_order() : 50;
    uint8_t j = i;
    while (j > 0) {
      EGModule* m_prev = EGPrefs::module_at(order[j - 1]);
      uint8_t o_prev = m_prev ? m_prev->display_order() : 50;
      if (o_prev <= o_cur) break;
      order[j] = order[j - 1];
      j--;
    }
    order[j] = cur;
  }

  bool first_emitted = true;
  for (uint8_t gi = 0; gi < gc; gi++) {
    EGModule* mod = EGPrefs::module_at(order[gi]);
    if (mod && mod->display_order() == 0) continue;  // hidden — has its own page
    const EGPrefGroup* g = EGPrefs::group_at(order[gi]);
    if (!g || g->count == 0) continue;

    n = snprintf_P(buf, sizeof(buf), PSTR("<details%S><summary>%s</summary>"),
                 first_emitted ? (PGM_P)PSTR(" open") : (PGM_P)PSTR(""),
                 g->label ? g->label : g->module_id);
    send_chunk(s, buf, (n > 0) ? (size_t)n : 0);
    first_emitted = false;

    for (size_t j = 0; j < g->count; j++) {
      const EGPref& p = g->prefs[j];
      if (p.flags & EGP_HIDDEN) continue;

      const char* cur = EGPrefs::getString(g->module_id, p.id);
      const char* mid = g->module_id;
      const char* pid = p.id;
      const char* lbl = p.label ? p.label : p.id;
      bool ro = (p.flags & EGP_READONLY);
      size_t pos = 0;

      n = snprintf_P(buf, sizeof(buf), PSTR("<label for='%s.%s'>%s</label><br/>"), mid, pid, lbl);
      if (n > 0 && (size_t)n < sizeof(buf)) pos = (size_t)n;

      if (p.type == EGP_BOOL) {
        n = snprintf_P(buf + pos, sizeof(buf) - pos,
                     PSTR("<input type='checkbox' id='%s.%s' name='%s.%s' value='1'%S%S><br/>"),
                     mid, pid, mid, pid,
                     (cur[0] == '1') ? (PGM_P)PSTR(" checked") : (PGM_P)PSTR(""),
                     ro ? (PGM_P)PSTR(" disabled") : (PGM_P)PSTR(""));
        advance_pos(pos, n, sizeof(buf));
      } else {
        const bool slider = (p.type == EGP_INT || p.type == EGP_UINT)
                         && (p.flags & EGP_SLIDER)
                         && p.min_i != p.max_i;
        const char* itype = "text";
        if (slider) itype = "range";
        else if (p.type == EGP_INT || p.type == EGP_UINT || p.type == EGP_FLOAT) itype = "number";
        else if (p.type == EGP_STRING && (p.flags & EGP_SENSITIVE)) itype = "password";
        else if (p.type == EGP_STRING && (p.flags & EGP_TIME))      itype = "time";

        n = snprintf_P(buf + pos, sizeof(buf) - pos,
                     PSTR("<input type='%s' id='%s.%s' name='%s.%s'"),
                     itype, mid, pid, mid, pid);
        advance_pos(pos, n, sizeof(buf));

        if (p.flags & EGP_SENSITIVE) {
          const char* ph = cur[0] ? "(unchanged)" : "(not set)";
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" value='' placeholder='%s'"), ph);
          advance_pos(pos, n, sizeof(buf));
        } else {
          APPEND_LIT(buf, pos, sizeof(buf), " value='");
          pos = append_escaped(buf, sizeof(buf), pos, cur);
          if (pos + 1 < sizeof(buf)) buf[pos++] = '\'';
          // Show the default value as placeholder when the field is empty.
          // Slider/time don't show placeholders so skip them.
          if (!slider && cur[0] == '\0'
              && p.default_val && p.default_val[0]
              && !(p.flags & EGP_TIME)) {
            n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" placeholder='%s'"), p.default_val);
            advance_pos(pos, n, sizeof(buf));
          }
        }

        if (p.type == EGP_STRING && p.max_len > 0) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" maxlength='%u'"), (unsigned)p.max_len);
          advance_pos(pos, n, sizeof(buf));
        }
        if (p.type == EGP_STRING && p.pattern && p.pattern[0]) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" pattern='%s'"), p.pattern);
          advance_pos(pos, n, sizeof(buf));
        }
        if ((p.type == EGP_INT || p.type == EGP_UINT || p.type == EGP_FLOAT) && p.min_i != p.max_i) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" min='%ld' max='%ld'"), (long)p.min_i, (long)p.max_i);
          advance_pos(pos, n, sizeof(buf));
        }
        if (p.type == EGP_FLOAT) APPEND_LIT(buf, pos, sizeof(buf), " step='any'");
        if (!slider && (p.type == EGP_INT || p.type == EGP_UINT)) {
          APPEND_LIT(buf, pos, sizeof(buf), " inputmode='numeric'");
        } else if (p.type == EGP_FLOAT) {
          APPEND_LIT(buf, pos, sizeof(buf), " inputmode='decimal'");
        }
        if (p.flags & EGP_REQUIRED) APPEND_LIT(buf, pos, sizeof(buf), " required");
        if (ro) APPEND_LIT(buf, pos, sizeof(buf), " readonly");
        if (slider) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos,
                       PSTR(" oninput='this.nextElementSibling.value=this.value'>"
                            "<output>%s</output><br/>"), cur);
          advance_pos(pos, n, sizeof(buf));
        } else {
          APPEND_LIT(buf, pos, sizeof(buf), "><br/>");
        }
      }

      if (p.help && p.help[0]) {
        n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("<small>%s</small>"), p.help);
        advance_pos(pos, n, sizeof(buf));
      }

      send_chunk(s, buf, pos);
    }

    s->sendContent(F("</details>"));
  }

  s->sendContent(F("<hr><button type='submit'>Save</button></form>"));

  s->sendContent(FPSTR(HTTP_BACKBTN));
  endChunkedPage();
}

#ifdef DEBUG_PREFS
void ConfigManager::handlePrefs() {
  auto* s = ConfigManager::server.get();
  if (!s->hasArg("m")) {
    s->send(400, "text/plain", "usage: /prefs?m=<module>&k=<key>  or  &action=delete");
    return;
  }
  String m_arg = s->arg("m");
  const char* m = m_arg.c_str();
  String action_arg = s->arg("action");
  if (strcmp(action_arg.c_str(), "delete") == 0) {
    bool ok = EGPrefs::remove_group(m);
    s->send(ok ? 200 : 404, "text/plain", ok ? "deleted" : "no file");
    return;
  }
  if (!s->hasArg("k")) {
    s->send(400, "text/plain", "missing k");
    return;
  }
  String k_arg = s->arg("k");
  const char* k = k_arg.c_str();
  const EGPref* p = EGPrefs::find_pref(m, k);
  if (!p) {
    s->send(404, "text/plain", "not found");
    return;
  }
  s->send(200, "text/plain", EGPrefs::getString(m, k));
}
#endif

void ConfigManager::handleClicksReturn()
{
  handleRequest();
  char buf[512];
  size_t pos = 0;
  int n;

  n = snprintf_P(buf + pos, sizeof(buf) - pos,
    PSTR("{\"last_day\":[%d"), (int)gcounter.clicks_hour);
  advance_pos(pos, n, sizeof(buf));

  const int histSize = gcounter.day_hourly_history.size();
  for (int i = histSize; i > 0; i--) {
    if (pos >= sizeof(buf) - 32) break;  // reserve tail for the JSON closer
    n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(",%d"),
      (int)gcounter.day_hourly_history[i - 1]);
    advance_pos(pos, n, sizeof(buf));
  }

  n = snprintf_P(buf + pos, sizeof(buf) - pos,
    PSTR("],\"today\":%d,\"yesterday\":%d,\"ratio\":\"%s\""),
    (int)gcounter.clicks_today,
    (int)gcounter.clicks_yesterday,
    EGPrefs::getString("sys", "ratio"));
  advance_pos(pos, n, sizeof(buf));

  if (ntpclient.synced) {
    n = snprintf_P(buf + pos, sizeof(buf) - pos,
      PSTR(",\"start\":%lu}"), (unsigned long)ntpclient.boot_epoch);
  } else {
    unsigned long uptime = NTP.getUptime() - start;
    n = snprintf_P(buf + pos, sizeof(buf) - pos,
      PSTR(",\"uptime\":%lu}"), uptime);
  }
  advance_pos(pos, n, sizeof(buf));
  buf[pos] = '\0';

  ConfigManager::server.get()->send(200, FPSTR(HTTP_HEAD_CTJSON), buf);
}


void ConfigManager::handleStatusPage()
{
  handleRequest();
  beginChunkedPage();
  char title[48];
  snprintf_P(title, sizeof(title), PSTR("%s - Status"), hostName);
  sendPageHead(title);
  auto* s = server.get();
  snprintf_P(title, sizeof(title), PSTR("%s - Status"), THING_NAME);
  { TemplateSub subs[] = {{"{v}", title}, {"{t}", hostName}};
    SEND_TEMPLATE(s, STATUS_PAGE_BODY_HEAD, subs); }

  {
    char seedBuf[240];
    size_t sp = 0;
    int sn;
    sn = snprintf_P(seedBuf, sizeof(seedBuf), PSTR("<script>window._seedHist=["));
    advance_pos(sp, sn, sizeof(seedBuf));
    int histSize = gcounter.cpm_history.size();
    bool firstHist = true;
    for (int i = 0; i < histSize; i += 3) {
      sn = snprintf_P(seedBuf + sp, sizeof(seedBuf) - sp,
                    firstHist ? PSTR("%d") : PSTR(",%d"), gcounter.cpm_history[i]);
      advance_pos(sp, sn, sizeof(seedBuf));
      firstHist = false;
    }
    sn = snprintf_P(seedBuf + sp, sizeof(seedBuf) - sp, PSTR("]</script>"));
    advance_pos(sp, sn, sizeof(seedBuf));
    s->sendContent(seedBuf, sp);
  }

  s->sendContent(FPSTR(STATUS_PAGE_BODY));
  s->sendContent(FPSTR(HTTP_BACKBTN));
  { TemplateSub subs[] = {{"{1}", RELEASE_VERSION}};
    SEND_TEMPLATE(s, STATUS_PAGE_FOOT, subs); }
  endChunkedPage();
}

void ConfigManager::handleInfo()
{
  handleRequest();
  beginChunkedPage();
  char title[48];
  snprintf_P(title, sizeof(title), PSTR("%s - Info"), THING_NAME);
  sendPageHead(title);
  auto* s = server.get();
  { TemplateSub subs[] = {{"{v}", title}, {"{t}", hostName}};
    SEND_TEMPLATE(s, STATUS_PAGE_BODY_HEAD, subs); }

  s->sendContent(F(
    "<style>"
    "details{border:1px solid #eee;padding:8px;margin:8px 0;max-width:680px;margin-left:auto;margin-right:auto}"
    "summary{font-weight:bold;padding:4px 0;cursor:pointer}"
    ".i{margin:auto;width:100%}"
    ".i th{text-align:left;padding-right:1em;vertical-align:top;font-weight:normal;color:#555}"
    ".i td{text-align:left;font-family:monospace;word-break:break-word}"
    ".m{list-style:none;padding:0;margin:0;columns:2;column-gap:1em}"
    ".m li{padding:2px 0}"
    "</style>"));

  char ip[16], gw[16], sn[16], dns[16], bssid[20], ssid[33], mac[18];
  char ap_ssid[33], ap_ip[16], ap_mac[18];
  strncpy(ip,      WiFi.localIP().toString().c_str(),      sizeof(ip)      - 1); ip[sizeof(ip) - 1] = '\0';
  strncpy(gw,      WiFi.gatewayIP().toString().c_str(),    sizeof(gw)      - 1); gw[sizeof(gw) - 1] = '\0';
  strncpy(sn,      WiFi.subnetMask().toString().c_str(),   sizeof(sn)      - 1); sn[sizeof(sn) - 1] = '\0';
  strncpy(dns,     WiFi.dnsIP().toString().c_str(),        sizeof(dns)     - 1); dns[sizeof(dns) - 1] = '\0';
  strncpy(bssid,   WiFi.BSSIDstr().c_str(),                sizeof(bssid)   - 1); bssid[sizeof(bssid) - 1] = '\0';
  strncpy(ssid,    WiFiManager::getWiFiSSID().c_str(),     sizeof(ssid)    - 1); ssid[sizeof(ssid) - 1] = '\0';
  strncpy(mac,     WiFi.macAddress().c_str(),              sizeof(mac)     - 1); mac[sizeof(mac) - 1] = '\0';
  strncpy(ap_ssid, WiFi.softAPSSID().c_str(),              sizeof(ap_ssid) - 1); ap_ssid[sizeof(ap_ssid) - 1] = '\0';
  strncpy(ap_ip,   WiFi.softAPIP().toString().c_str(),     sizeof(ap_ip)   - 1); ap_ip[sizeof(ap_ip) - 1] = '\0';
  strncpy(ap_mac,  WiFi.softAPmacAddress().c_str(),        sizeof(ap_mac)  - 1); ap_mac[sizeof(ap_mac) - 1] = '\0';

  char buf[256];
  int n;

  // PSTR the label + template into flash; literal `fmt` concatenates into the
  // surrounding PSTR string. `%S` (capital) reads the PGM-resident label.
  // This keeps ~25 label literals out of .rodata (SRAM on ESP8266).
  #define INFO_ROW(k, fmt, ...) do { \
    n = snprintf_P(buf, sizeof(buf), \
        PSTR("<tr><th>%S</th><td>" fmt "</td></tr>"), \
        (PGM_P)PSTR(k), ##__VA_ARGS__); \
    if (n > 0) s->sendContent(buf, (size_t)n); \
  } while (0)

  n = snprintf_P(buf, sizeof(buf),
    PSTR("<p style='text-align:center'>Connected to <b>%s</b> with IP <b>%s</b> &middot; %s</p>"),
    ssid[0] ? ssid : "(not connected)", ip, DeviceInfo::chipmodel());
  if (n > 0) s->sendContent(buf, (size_t)n);

  s->sendContent(F("<details open><summary>System</summary><table class='i'>"));
  INFO_ROW("Uptime",       "%s",            DeviceInfo::uptimeString());
  INFO_ROW("Chip ID",      "%s",            chipId);
#ifdef ESP8266
  INFO_ROW("Platform",     "%s",            "esp8266");
  INFO_ROW("Flash chip ID","%u",            (unsigned)ESP.getFlashChipId());
  INFO_ROW("Flash size",   "%u bytes",      (unsigned)ESP.getFlashChipSize());
  INFO_ROW("Real flash",   "%u bytes",      (unsigned)ESP.getFlashChipRealSize());
  INFO_ROW("Core version", "%s",            ESP.getCoreVersion().c_str());
  INFO_ROW("Boot version", "%u",            (unsigned)ESP.getBootVersion());
#elif defined(ESP32)
  INFO_ROW("Platform",     "%s",            ESP.getChipModel());   // ESP32, ESP32-S3, ESP32-C3 ...
  INFO_ROW("Flash size",   "%u bytes",      (unsigned)ESP.getFlashChipSize());
  INFO_ROW("Chip rev",     "%u",            (unsigned)ESP.getChipRevision());
  INFO_ROW("SDK version",  "%s",            ESP.getSdkVersion());
#endif
  INFO_ROW("CPU frequency","%u MHz",        (unsigned)ESP.getCpuFreqMHz());
  INFO_ROW("Free heap",    "%u bytes",      (unsigned)ESP.getFreeHeap());
  INFO_ROW("Sketch size",  "%u / %u bytes", (unsigned)ESP.getSketchSize(),
                                            (unsigned)(ESP.getSketchSize() + ESP.getFreeSketchSpace()));
#ifdef ESP8266
  INFO_ROW("Last reset",   "%s",            ESP.getResetReason().c_str());
  {
    rst_info* ri = ESP.getResetInfoPtr();
    if (ri && ri->reason == REASON_EXCEPTION_RST) {
      INFO_ROW("Exc cause", "%u",     (unsigned)ri->exccause);
      INFO_ROW("Exc PC",    "0x%08x", (unsigned)ri->epc1);
      INFO_ROW("Exc addr",  "0x%08x", (unsigned)ri->excvaddr);
    }
  }
#elif defined(ESP32)
  {
    const char* rstStr;
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:   rstStr = "Power on"; break;
      case ESP_RST_EXT:       rstStr = "External reset"; break;
      case ESP_RST_SW:        rstStr = "Software restart"; break;
      case ESP_RST_PANIC:     rstStr = "Exception/panic"; break;
      case ESP_RST_INT_WDT:   rstStr = "Interrupt watchdog"; break;
      case ESP_RST_TASK_WDT:  rstStr = "Task watchdog"; break;
      case ESP_RST_WDT:       rstStr = "Watchdog"; break;
      case ESP_RST_BROWNOUT:  rstStr = "Brownout"; break;
      case ESP_RST_DEEPSLEEP: rstStr = "Deep sleep wake"; break;
      default:                rstStr = "Unknown"; break;
    }
    INFO_ROW("Last reset", "%s", rstStr);
  }
#endif
  s->sendContent(F("</table></details>"));

  // --- WiFi -----------------------------------------------------------
  s->sendContent(F("<details open><summary>WiFi</summary><table class='i'>"));
  INFO_ROW("Connected",     "%s",     WiFi.isConnected() ? "Yes" : "No");
  INFO_ROW("Station SSID",  "%s",     ssid);
  INFO_ROW("Station IP",    "%s",     ip);
  INFO_ROW("Station gateway","%s",    gw);
  INFO_ROW("Station subnet","%s",     sn);
  INFO_ROW("DNS Server",    "%s",     dns);
  INFO_ROW("Hostname",      "%s",     hostName);
  INFO_ROW("Station MAC",   "%s",     mac);
  INFO_ROW("RSSI",          "%d dBm", (int)Wifi::rssi);
  INFO_ROW("BSSID",         "%s",     bssid);
  INFO_ROW("Autoconnect",   "%s",     WiFi.getAutoReconnect() ? "Enabled" : "Disabled");
  INFO_ROW("AP SSID",       "%s",     ap_ssid[0] ? ap_ssid : "(not active)");
  INFO_ROW("AP IP",         "%s",     ap_ip);
  INFO_ROW("AP MAC",        "%s",     ap_mac);
  s->sendContent(F("</table></details>"));

  // --- Modules --------------------------------------------------------
  s->sendContent(F("<details open><summary>Modules</summary><ul class='m'>"));
  {
    uint8_t count = EGModuleRegistry::count();
    for (uint8_t i = 0; i < count; i++) {
      EGModule* m = EGModuleRegistry::get(i);
      if (m == nullptr) continue;
      n = snprintf_P(buf, sizeof(buf), PSTR("<li>%s</li>"), m->name());
      if (n > 0) s->sendContent(buf, (size_t)n);
    }
  }
  s->sendContent(F("</ul></details>"));

  // --- About ----------------------------------------------------------
  s->sendContent(F("<details open><summary>About</summary><table class='i'>"));
  INFO_ROW("Version",    "%s",    RELEASE_VERSION);
  INFO_ROW("Git",        "%s",    GIT_VERSION);
  INFO_ROW("Build env",  "%s",    BUILD_ENV);
  INFO_ROW("Build date", "%s %s", __DATE__, __TIME__);
  INFO_ROW("Geiger",     "%s",    DeviceInfo::geigermodel());
  INFO_ROW("Feature flags","0x%04x", (unsigned)DeviceInfo::featureFlags());
  s->sendContent(F("</table></details>"));

  s->sendContent(F("<details><summary>Actions</summary>"));
  s->sendContent(FPSTR(HTTP_UPDATEBTN));
  s->sendContent(F(
    "<br/><form action='/erase' method='get'"
    " onsubmit=\"return confirm('Erase saved WiFi credentials? Device will reboot into captive-portal mode.')\">"
    "<button class='D'>Erase WiFi config</button></form>"));
  s->sendContent(F("</details>"));

  #undef INFO_ROW
  s->sendContent(FPSTR(HTTP_BACKBTN));
  endChunkedPage();
}

void ConfigManager::handleHistoryPage()
{
  handleRequest();
  beginChunkedPage();
  char title[48];
  snprintf_P(title, sizeof(title), PSTR("%s - History"), hostName);
  sendPageHead(title);
  auto* s = server.get();
  snprintf_P(title, sizeof(title), PSTR("%s - History"), THING_NAME);
  { TemplateSub subs[] = {{"{v}", title}, {"{t}", hostName}};
    SEND_TEMPLATE(s, STATUS_PAGE_BODY_HEAD, subs); }
  s->sendContent(FPSTR(HISTORY_PAGE_BODY));
  s->sendContent(FPSTR(HISTORY_PAGE_JS));
  s->sendContent(FPSTR(HTTP_BACKBTN));
  endChunkedPage();
}

void ConfigManager::handleNTP()
{
  handleRequest();
  beginChunkedPage();
  char title[48];
  snprintf_P(title, sizeof(title), PSTR("%s - NTP"), THING_NAME);
  sendPageHead(title);
  auto* s = server.get();
  { TemplateSub subs[] = {{"{i}", ntpclient.get_server()}, {"{v}", ntpclient.get_tz()}};
    SEND_TEMPLATE(s, NTP_HTML, subs); }
  s->sendContent(FPSTR(NTP_TZ_JS));
  s->sendContent(FPSTR(NTP_JS));
  s->sendContent(FPSTR(HTTP_BACKBTN));
  endChunkedPage();
}

void ConfigManager::handleNTPSet()
{
  handleRequest();
  EGPrefs::put("ntp", "server", server->arg("s").c_str());
  EGPrefs::put("ntp", "tz",     server->arg("t").c_str());
  EGPrefs::commit();  // triggers NTP_Client::on_prefs_saved -> setup()

  beginChunkedPage();
  char title[48];
  snprintf_P(title, sizeof(title), PSTR("%s - NTP Saved"), THING_NAME);
  auto* s = server.get();
  { TemplateSub subs[] = {{"{v}", title}};
    SEND_TEMPLATE(s, HTTP_HEAD_START, subs); }
  s->sendContent(FPSTR(HTTP_STYLE));
  s->sendContent(FPSTR(faviconHead));
  s->sendContent(FPSTR(HTTP_HEAD_MREFRESH));
  s->sendContent(FPSTR(HTTP_HEAD_END));
  s->sendContent(FPSTR(HTTP_PARAMSAVED));
  s->sendContent(FPSTR(HTTP_BACKBTN));
  endChunkedPage();
}

#ifdef ESPGEIGER_HW
void ConfigManager::handleHVPage()
{
  handleRequest();
  beginChunkedPage();
  char title[48];
  snprintf_P(title, sizeof(title), PSTR("%s-HW - HV"), THING_NAME);
  sendPageHead(title);
  auto* s = server.get();
  { TemplateSub subs[] = {{"{v}", title}, {"{t}", hostName}};
    SEND_TEMPLATE(s, STATUS_PAGE_BODY_HEAD, subs); }
  s->sendContent(FPSTR(HV_STATUS_PAGE_BODY));
  s->sendContent(FPSTR(HTTP_BACKBTN));
  endChunkedPage();
}

void ConfigManager::handleHVSet()
{
  handleRequest();
  int _d = atoi(server->arg(F("d")).c_str());
  int _f = atoi(server->arg(F("f")).c_str());
  int _r = atoi(server->arg(F("r")).c_str());
  hardware.set_duty(_d);
  hardware.set_freq(_f);
  hardware.set_vd_ratio(_r);
  hardware.saveconfig();
  ConfigManager::server->send(200, FPSTR(HTTP_HEAD_CT), F("OK"));
}

void ConfigManager::handleHVJSReturn()
{
  handleRequest();
  beginChunkedPage(HTTP_HEAD_CTJS);
  server->sendContent(FPSTR(picographJS));
  server->sendContent(FPSTR(hvJS));
  server->sendContent("");
  server->client().stop();
}

void ConfigManager::handleHVJsonReturn()
{
  handleRequest();
  char jsonBuffer[256] = "";

  int total = sizeof(jsonBuffer);
  int volts = hardware.hvReading.get();
  int freq = hardware.get_freq();
  int duty = hardware.get_duty();
  int ratio = hardware.get_vd_ratio();

  snprintf_P (
    jsonBuffer,
    sizeof(jsonBuffer),
    PSTR("{\"volts\":%d,\"freq\":%d,\"duty\":%d,\"ratio\":%d}"),
    volts,
    freq,
    duty,
    ratio
  );
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  ConfigManager::server.get()->send ( 200, FPSTR(HTTP_HEAD_CTJSON), jsonBuffer );
}
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
void ConfigManager::handleSetCPM() {
  handleRequest();
  int _d = atoi(server->arg(F("v")).c_str());
  if (_d > 0) {
    gcounter.set_target_cpm(_d);
  }
  ConfigManager::server->send(200, FPSTR(HTTP_HEAD_CT), F("OK"));
}
#endif

void ConfigManager::handleRestart()
{
  handleRequest();
  beginChunkedPage();
  auto* s = server.get();
  { TemplateSub subs[] = {{"{v}", "Restarting..."}};
    SEND_TEMPLATE(s, HTTP_HEAD_START, subs); }
  s->sendContent(FPSTR(HTTP_STYLE));
  s->sendContent(FPSTR(faviconHead));
  s->sendContent(FPSTR(HTTP_HEAD_MREFRESH));
  s->sendContent(FPSTR(HTTP_HEAD_END));
  s->sendContent(FPSTR(THING_NAME));
  s->sendContent(F(" is restarting...<br><br>"));
  endChunkedPage();
  Log::console(PSTR("Config: Restarting ... "));
  delay(1000);
  ESP.restart();
}

void ConfigManager::eraseSettings()
{
  Log::console(PSTR("Config: Erasing ... "));
  LittleFS.begin();
  LittleFS.remove("/geigerconfig.json");
  LittleFS.remove("/ntp.json");
  LittleFS.end();
  WiFiManager::resetSettings();
}

void ConfigManager::handleResetParams()
{
  Log::console(PSTR("Config: Erasing ... "));
  LittleFS.begin();
  LittleFS.remove("/geigerconfig.json");
  LittleFS.remove("/ntp.json");
  LittleFS.end();
  handleRestart();
}
void ConfigManager::delay(unsigned long m)
{
  unsigned long delayStart = millis();
  while (m > millis() - delayStart)
  {
    this->process();
    // -- Note: 1ms might not be enough to perform a full yield. So
    // 'yield' in 'doLoop' is eventually a good idea.
    delayMicroseconds(1000);
    yield();
  }
}

// Self-throttled wrapper around WiFiManager::process().
//   ESP32: sync WebServer.handleClient() yields 1ms per call via lwIP
//     socket timeouts, capping loop() LPS to ~1k if unthrottled. 10 ms
//     polling (100 Hz) keeps HTTP responsive with huge loop headroom.
//   ESP8266: handler is cheap per call but not free (~3-5 µs each) —
//     still meaningful at 20-50k iter/sec. 5 ms keeps captive-portal DNS
//     snappy without eating ~10% of CPU on idle polling.
// Override either via -D CMAN_PROCESS_INTERVAL_MS=N.
#ifndef CMAN_PROCESS_INTERVAL_MS
#ifdef ESP32
#define CMAN_PROCESS_INTERVAL_MS 10
#else
#define CMAN_PROCESS_INTERVAL_MS 5
#endif
#endif
void ConfigManager::processLoop(unsigned long now)
{
  static unsigned long last = 0;
  if (now - last >= CMAN_PROCESS_INTERVAL_MS) {
    last = now;
    this->process();
  }
}

void ConfigManager::handleRefreshConsole()
{
  handleRequest();
  uint32_t counter = 0;

  char stmp[8];
  String s = server->arg("c1");
  strncpy(stmp, s.c_str(), sizeof(stmp) - 1);
  stmp[sizeof(stmp) - 1] = '\0';
  if (strlen(stmp))
  {
    counter = atoi(stmp);
  }
  server->client().flush();
  server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  server->sendHeader(F("Pragma"), F("no-cache"));
  server->sendHeader(F("Expires"), F("-1"));
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, F("text/plain"), "");
  // "<log_idx>,<tz_offset_min>\n" - tz hitch-hikes here so JS can convert
  // log HH:MM:SS to browser-local. Only consumer needs it, so don't bloat /json.
  char idxBuf[16];
  snprintf_P(idxBuf, sizeof(idxBuf), PSTR("%u,%d\n"), (uint8_t)Log::getLogIdx(), ntpclient.tz_offset_min);
  server->sendContent(idxBuf);
  if (counter != Log::getLogIdx())
  {
    if (!counter)
    {
      counter = Log::getLogIdx();
    }
    do
    {
      char *tmp;
      size_t len;
      Log::getLog(counter, &tmp, &len);
      if (len)
      {
        char stemp[256];
        size_t copyLen = (len < sizeof(stemp) - 1) ? len : sizeof(stemp) - 1;
        memcpy(stemp, tmp, copyLen);
        stemp[copyLen - 1] = '\n';
        stemp[copyLen] = '\0';
        server->sendContent(stemp);
      }
      counter++;
      counter &= 0xFF;
      if (!counter)
      {
        counter++;
      } // Skip log index 0 as it is not allowed
    } while (counter != Log::getLogIdx());
  }

  server->sendContent("");
  server->client().stop();
}


void ConfigManager::bindServerCallback()
{
  ConfigManager::server.get()->on(ROOT_URL, HTTP_GET, std::bind(&ConfigManager::handleRoot, this));
  ConfigManager::server.get()->on(STATUS_URL, HTTP_GET, std::bind(&ConfigManager::handleStatusPage, this));
  ConfigManager::server.get()->on(JSON_URL, HTTP_GET, std::bind(&ConfigManager::handleJsonReturn, this));
  ConfigManager::server.get()->on(JS_URL, HTTP_GET, std::bind(&ConfigManager::handleJSReturn, this));
  ConfigManager::server.get()->on(CONSOLE_URL, HTTP_GET, std::bind(&ConfigManager::handleRefreshConsole, this));
  ConfigManager::server.get()->on(RESTART_URL, HTTP_GET, std::bind(&ConfigManager::handleRestart, this));
  ConfigManager::server.get()->on(RESET_URL, HTTP_GET, std::bind(&ConfigManager::handleResetParams, this));
  ConfigManager::server.get()->on(NTP_URL, HTTP_GET, std::bind(&ConfigManager::handleNTP, this));
  ConfigManager::server.get()->on(NTP_SET_URL, HTTP_POST, std::bind(&ConfigManager::handleNTPSet, this));
  ConfigManager::server.get()->on(CLICKS_JSON, HTTP_GET, std::bind(&ConfigManager::handleClicksReturn, this));
  ConfigManager::server.get()->on(HIST_URL, HTTP_GET, std::bind(&ConfigManager::handleHistoryPage, this));
  ConfigManager::server.get()->on(GEIGERLOG_URL, HTTP_GET, std::bind(&ConfigManager::handleGeigerLog, this));
#ifdef SERIALOUT
  ConfigManager::server.get()->on(SERIAL_URL, HTTP_GET, std::bind(&ConfigManager::handleSerialOut, this));
#endif
  ConfigManager::server.get()->on(ABOUT_URL, HTTP_GET, std::bind(&ConfigManager::handleAbout, this));
  ConfigManager::server.get()->on(INFO_URL,  HTTP_GET, std::bind(&ConfigManager::handleInfo, this));
  ConfigManager::server.get()->on(OUTPUTS_URL, HTTP_GET, std::bind(&ConfigManager::handleOutputsJson, this));
#ifdef DEBUG_PREFS
  ConfigManager::server.get()->on(PREFS_URL, HTTP_GET, std::bind(&ConfigManager::handlePrefs, this));
#endif
  ConfigManager::server.get()->on(EGPREFS_URL, HTTP_GET,  std::bind(&ConfigManager::handleEGPrefs, this));
  ConfigManager::server.get()->on(EGPREFS_URL, HTTP_POST, std::bind(&ConfigManager::handleEGPrefs, this));
#ifdef ESPGEIGER_HW
  ConfigManager::server.get()->on(HV_URL, HTTP_GET, std::bind(&ConfigManager::handleHVPage, this));
  ConfigManager::server.get()->on(HV_SET_URL, HTTP_GET, std::bind(&ConfigManager::handleHVSet, this));
  ConfigManager::server.get()->on(HV_JS_URL, HTTP_GET, std::bind(&ConfigManager::handleHVJSReturn, this));
  ConfigManager::server.get()->on(HV_JSON_URL, HTTP_GET, std::bind(&ConfigManager::handleHVJsonReturn, this));
#endif
#if GEIGER_IS_TEST(GEIGER_TYPE)
  ConfigManager::server.get()->on(SETCPM_URL, HTTP_GET, std::bind(&ConfigManager::handleSetCPM, this));
#endif
}

