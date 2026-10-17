/*
  NTP.cpp - functions to handle NTP

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
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <EGHttpServer.h>
#include <EGTimeZone.h>
#include <EGEscape.h>
#include "NTP.h"
#include "../System/Globals.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../System/Clock.h"
#include "../WebPortal/WebPortal.h"
#include "NTP_PAGE_JS.gz.h"

NTP_Client ntpclient = NTP_Client();
EG_REGISTER_MODULE(ntpclient)
#ifdef ESP32
#include "sntp.h"
#endif

EG_PSTR(NT_L_SRV, "NTP Server");
EG_PSTR(NT_L_TZ,  "Timezone");
EG_PSTR(NT_H_TZ,  "Olson name");
EG_PSTR(NT_P_TZ,  "[A-Za-z0-9/_+\\-]+");
#define NTP_TZ_MAX 64

static const EGPref NTP_PREF_ITEMS[] PROGMEM = {
  {EG_K_SERVER, NT_L_SRV, nullptr,  NTP_SERVER, EG_P_HOST, 0, 0, EG_HOST_MAX, EGP_STRING, EGP_ADVANCED},
  {EG_K_TZ,     NT_L_TZ,  NT_H_TZ,  NTP_TZ,     NT_P_TZ,   0, 0, NTP_TZ_MAX, EGP_STRING, 0},
};

// sntp keeps the pointer, a save frees the pref's
static char s_server[EG_HOST_MAX + 1];
// set by sntp, the tick applies it
static volatile bool s_synced = false;

static const EGPrefGroup NTP_PREF_GROUP PROGMEM = {
  "ntp", "NTP", 1,
  NTP_PREF_ITEMS,
  sizeof(NTP_PREF_ITEMS) / sizeof(NTP_PREF_ITEMS[0]),
};

const EGPrefGroup* NTP_Client::prefs_group() { return &NTP_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias NTP_LEGACY[] PROGMEM = {
  {"srv", "server"},
  {"tz",  "tz"},
  {},
};
EGLegacyRows NTP_Client::legacy_aliases() { return NTP_LEGACY; }
// === END LEGACY IMPORT ===

NTP_Client::NTP_Client() {
}

void NTP_Client::setup()
{
#ifndef DISABLE_NTP
  const char* server = EGPrefs::getString("ntp", EG_K_SERVER);
  if (server[0] == '\0') server = NTP_SERVER;
  if (strcmp(s_server, server) != 0) strlcpy(s_server, server, sizeof(s_server));
  server = s_server;
  const char* tz = EGPrefs::getString("ntp", EG_K_TZ);
  if (tz[0] == '\0') tz = NTP_TZ;

  Log::console(PSTR("NTP: Starting ... %s"), server);
  const char *possixTZ = EGTimeZone::posixFor(tz);
  if (!possixTZ) {
    Log::console(PSTR("NTP: timezone '%s' not known, using UTC"), tz);
    possixTZ = PSTR("UTC0");
  }
#ifdef ESP8266
  settimeofday_cb([](){
    if (Clock::epoch() < Clock::MIN_EPOCH) return;  // SDK fires once before sync
    s_synced = true;
  });

  configTime(possixTZ, server);
#else
  // tcpip task, never wait here
  sntp_set_time_sync_notification_cb([](struct timeval *t){
    if (!t || (uint32_t)t->tv_sec < Clock::MIN_EPOCH) return;
    s_synced = true;
  });
  configTime(0, 0, server); // TZ set below
  setenv("TZ", possixTZ, 1); // Set environment variable with your time zone
  tzset();
#endif
#endif
}

void NTP_Client::on_prefs_saved() {
  setup();
}

static void take_sync() {
  s_synced = false;
  if (Clock::on_sync()) Log::console(PSTR("NTP: Synched"));
}

// first sync only, loop() takes resyncs; off once either has it
void NTP_Client::s_tick(unsigned long) {
  if (s_synced) take_sync();
  if (Clock::synced) EGModuleRegistry::set_tick_enabled(this, false);
}

void NTP_Client::loop(unsigned long now) {
  if (s_synced) take_sync();
  if (Clock::resync_due(now)) setup();
  if (Clock::synced) Clock::refresh_tz_offset_min();
}

// HTTP routes

// no comments, ships to the browser
#if !EG_GZ_NTP_PAGE_JS
extern const char NTP_PAGE_JS[] PROGMEM = R"HTML(
var L={"Asia":{"Asia":["Almaty","Amman","Aqtau","Aqtobe","Ashgabat","Atyrau","Baghdad","Baku","Bangkok","Beirut","Bishkek","Colombo","Damascus","Dhaka","Dili","Dubai","Dushanbe","Famagusta","Gaza","Hebron","Ho_Chi_Minh","Hong_Kong","Hovd","Jakarta","Jayapura","Jerusalem","Kabul","Karachi","Kathmandu","Kolkata","Kuching","Macau","Makassar","Manila","Nicosia","Oral","Pontianak","Pyongyang","Qatar","Qostanay","Qyzylorda","Riyadh","Samarkand","Seoul","Shanghai","Singapore","Taipei","Tashkent","Tbilisi","Tehran","Thimphu","Tokyo","Ulaanbaatar","Urumqi","Yangon","Yerevan"],"Indian":["Chagos","Maldives"]},"Africa":{"Africa":["Abidjan","Algiers","Bissau","Cairo","Casablanca","El_Aaiun","Johannesburg","Juba","Khartoum","Lagos","Maputo","Monrovia","Nairobi","Ndjamena","Sao_Tome","Tripoli","Tunis","Windhoek"],"Atlantic":["Cape_Verde"],"Indian":["Mauritius"]},"Europe":{"Africa":["Ceuta"],"America":["Danmarkshavn","Nuuk","Scoresbysund","Thule"],"Asia":["Anadyr","Barnaul","Chita","Irkutsk","Kamchatka","Khandyga","Krasnoyarsk","Magadan","Novokuznetsk","Novosibirsk","Omsk","Sakhalin","Srednekolymsk","Tomsk","Ust-Nera","Vladivostok","Yakutsk","Yekaterinburg"],"Atlantic":["Azores","Canary","Faroe","Madeira"],"Europe":["Andorra","Astrakhan","Athens","Belgrade","Berlin","Brussels","Bucharest","Budapest","Chisinau","Dublin","Gibraltar","Helsinki","Istanbul","Kaliningrad","Kirov","Kyiv","Lisbon","London","Madrid","Malta","Minsk","Moscow","Paris","Prague","Riga","Rome","Samara","Saratov","Simferopol","Sofia","Tallinn","Tirane","Ulyanovsk","Vienna","Vilnius","Volgograd","Warsaw","Zurich"]},"North America":{"America":["Adak","Anchorage","Bahia_Banderas","Barbados","Belize","Boise","Cambridge_Bay","Cancun","Chicago","Chihuahua","Ciudad_Juarez","Costa_Rica","Dawson","Dawson_Creek","Denver","Detroit","Edmonton","El_Salvador","Fort_Nelson","Glace_Bay","Goose_Bay","Grand_Turk","Guatemala","Halifax","Havana","Hermosillo","Indiana/Indianapolis","Indiana/Knox","Indiana/Marengo","Indiana/Petersburg","Indiana/Tell_City","Indiana/Vevay","Indiana/Vincennes","Indiana/Winamac","Inuvik","Iqaluit","Jamaica","Juneau","Kentucky/Louisville","Kentucky/Monticello","Los_Angeles","Managua","Martinique","Matamoros","Mazatlan","Menominee","Merida","Metlakatla","Mexico_City","Miquelon","Moncton","Monterrey","New_York","Nome","North_Dakota/Beulah","North_Dakota/Center","North_Dakota/New_Salem","Ojinaga","Panama","Phoenix","Port-au-Prince","Puerto_Rico","Rankin_Inlet","Regina","Resolute","Santo_Domingo","Sitka","St_Johns","Swift_Current","Tegucigalpa","Tijuana","Toronto","Vancouver","Whitehorse","Winnipeg","Yakutat"],"Atlantic":["Bermuda"],"Pacific":["Honolulu"]},"South America":{"America":["Araguaina","Argentina/Buenos_Aires","Argentina/Catamarca","Argentina/Cordoba","Argentina/Jujuy","Argentina/La_Rioja","Argentina/Mendoza","Argentina/Rio_Gallegos","Argentina/Salta","Argentina/San_Juan","Argentina/San_Luis","Argentina/Tucuman","Argentina/Ushuaia","Asuncion","Bahia","Belem","Boa_Vista","Bogota","Campo_Grande","Caracas","Cayenne","Coyhaique","Cuiaba","Eirunepe","Fortaleza","Guayaquil","Guyana","La_Paz","Lima","Maceio","Manaus","Montevideo","Noronha","Paramaribo","Porto_Velho","Punta_Arenas","Recife","Rio_Branco","Santarem","Santiago","Sao_Paulo"],"Atlantic":["South_Georgia","Stanley"],"Pacific":["Easter","Galapagos"]},"Australia":{"Australia":["Adelaide","Brisbane","Broken_Hill","Darwin","Eucla","Hobart","Lindeman","Lord_Howe","Melbourne","Perth","Sydney"]},"Pacific":{"Pacific":["Apia","Auckland","Bougainville","Chatham","Efate","Fakaofo","Fiji","Gambier","Guadalcanal","Guam","Kanton","Kiritimati","Kosrae","Kwajalein","Marquesas","Nauru","Niue","Norfolk","Noumea","Pago_Pago","Palau","Pitcairn","Port_Moresby","Rarotonga","Tahiti","Tarawa","Tongatapu"]},"Antarctica":{"Antarctica":["Casey","Davis","Macquarie","Mawson","Palmer","Rothera","Troll","Vostok"]},"Etc":{"Etc":["GMT-14","GMT-13","GMT-12","GMT-11","GMT-10","GMT-9","GMT-8","GMT-7","GMT-6","GMT-5","GMT-4","GMT-3","GMT-2","GMT-1","GMT","GMT+0","GMT-0","GMT+1","GMT+2","GMT+3","GMT+4","GMT+5","GMT+6","GMT+7","GMT+8","GMT+9","GMT+10","GMT+11","GMT+12","GMT0","Greenwich","UCT","UTC","Universal","Zulu"]}};
var x = byID("tz");
var s = x.getAttribute('data-option')||'Etc/UTC';
var k = x.getAttribute('data-u')!='1';
var f = false;
for (var c in L) {
  var og = document.createElement("optgroup");
  og.label = c;
  for (var p in L[c]) L[c][p].forEach(t => {
    var z = p + '/' + t;
    og.appendChild(new Option(z, z, 0, z == s));
    if (z == s) f = true;
  });
  x.add(og);
}
function P(v, l) {
  x.value = v;
  if (x.value != v) { x.insertBefore(new Option(l||v, v), x.firstChild); x.value = v; }
}
if (!f) P(s, s + (k ? ' (not listed)' : ' (not recognised - please reselect)'));
var b = document.createElement("button");
x.parentNode.insertBefore(b, x.nextSibling);
b.type = 'button';
b.className = 'btn-sm btn-o r';
b.textContent = 'Detect';
b.onclick = () => { var t = Intl.DateTimeFormat().resolvedOptions().timeZone; if (t) P(t); };
)HTML";
#endif

void NTP_Client::renderInlineForm(EGHttpResponse& res) {
  // data-u 1 = name not known
  char u[2] = { EGTimeZone::posixFor(get_tz()) ? '0' : '1', '\0' };
  // one buffer, tz then server
  static_assert(NTP_TZ_MAX <= EG_HOST_MAX, "esc too small for tz");
  char esc[6 * EG_HOST_MAX + 1];
  res.sendKV(F("<form method=POST action=/ntpset>"
               "<label for=tz>Timezone</label>"
               "<select name=t id=tz data-u='"),
             u,
             F("' data-option='"));
  egesc_html(esc, sizeof(esc), get_tz());
  res.sendKV(nullptr,
             esc,
             F("'></select>"
               "<label for=ntps>NTP Server</label>"
               "<input id=ntps name=s maxlength=" STR(EG_HOST_MAX) " value='"));
  egesc_html(esc, sizeof(esc), get_server());
  res.sendKV(nullptr, esc, F("'><button type=submit>Save</button></form>"));
}

static void hNtpJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if EG_GZ_NTP_PAGE_JS
  res.sendGzipP(200, FPSTR(HTTP_CT_JS), NTP_PAGE_JS_GZ, NTP_PAGE_JS_GZ_LEN);
#else
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(NTP_PAGE_JS));
#endif
}

static const char NTP_SAVED_BODY[] PROGMEM =
  "<p>NTP settings saved. The new timezone and server are in effect now.</p>";
static const char NTP_REFUSED_BODY[] PROGMEM =
  "<div class='a ae'>Not all saved: the server name or timezone was not valid.</div>";

static void hNtpSet(EGHttpRequest& req, EGHttpResponse& res, void*) {
  bool ok = true;
  // arg() reuses its buffer
  const char* s = req.arg(F("s"));
  if (s && !EGPrefs::put("ntp", EG_K_SERVER, s)) ok = false;
  const char* t = req.arg(F("t"));
  if (t && !EGPrefs::put("ntp", EG_K_TZ,  t)) ok = false;
  EGPrefs::commit();

  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, ok ? F("NTP Saved") : F("NTP"));
  res.sendChunk(ok ? FPSTR(NTP_SAVED_BODY) : FPSTR(NTP_REFUSED_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

void NTP_Client::registerRoutes(EGHttpServer& http) {
  http.on(F("/ntpjs"),  EGHttpRequest::GET,  hNtpJs);
  http.on(F("/ntpset"), EGHttpRequest::POST, hNtpSet);
}
