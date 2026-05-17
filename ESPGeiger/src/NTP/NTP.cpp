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
#include <EGHttpServer.h>
#include "NTP.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
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

static const EGPref NTP_PREF_ITEMS[] = {
  {"server", NT_L_SRV, nullptr,  NTP_SERVER, nullptr, 0, 0, 64, EGP_STRING, 0},
  {"tz",     NT_L_TZ,  NT_H_TZ,  NTP_TZ,     nullptr, 0, 0, 64, EGP_STRING, 0},
};

static const EGPrefGroup NTP_PREF_GROUP = {
  "ntp", "NTP", 1,
  NTP_PREF_ITEMS,
  sizeof(NTP_PREF_ITEMS) / sizeof(NTP_PREF_ITEMS[0]),
};

const EGPrefGroup* NTP_Client::prefs_group() { return &NTP_PREF_GROUP; }

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias NTP_LEGACY[] = {
  {"srv", "server"},
  {"tz",  "tz"},
  {nullptr, nullptr},
};
const EGLegacyAlias* NTP_Client::legacy_aliases() { return NTP_LEGACY; }
// === END LEGACY IMPORT ===

NTP_Client::NTP_Client() {
}

void NTP_Client::setup()
{
#ifndef DISABLE_NTP
  const char* server = EGPrefs::getString("ntp", "server");
  if (server[0] == '\0') server = NTP_SERVER;
  const char* tz = EGPrefs::getString("ntp", "tz");
  if (tz[0] == '\0') tz = NTP_TZ;

  Log::console(PSTR("NTP: Starting ... %s"), server);
  const char *possixTZ = getPosixTZforOlson(tz);
#ifdef ESP8266
  settimeofday_cb([](){
    ntpclient.last_sync_ms = millis();
    if (!ntpclient.synced) {
      ntpclient.synced = true;
      unsigned long uptime = ntpclient.getUptime() - start;
      ntpclient.boot_epoch = (unsigned long)time(NULL) - uptime;
      Log::console(PSTR("NTP: Synched"));
    }
  });

  configTime(possixTZ, server);
#else
  sntp_set_time_sync_notification_cb([](struct timeval *t){
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)){
      return;
    }
    ntpclient.last_sync_ms = millis();
    if (!ntpclient.synced) {
      ntpclient.synced = true;
      unsigned long uptime = ntpclient.getUptime() - start;
      ntpclient.boot_epoch = (unsigned long)time(NULL) - uptime;
      Log::console(PSTR("NTP: Synched"));
    }
  });
  configTime(0, 0, server); // 0, 0 because we will use TZ in the next line
  setenv("TZ", possixTZ, 1); // Set environment variable with your time zone
  tzset();
#endif
#endif
}

void NTP_Client::on_prefs_saved() {
  setup();  // re-apply server/tz without reboot
}

// ---------- HTTP routes ----------

// When EG_GZ_NTP_PAGE_JS is set the gzipped blob in NTP_PAGE_JS.gz.h
// replaces this; the raw R"..." then compiles out.
#if !EG_GZ_NTP_PAGE_JS
extern const char NTP_PAGE_JS[] PROGMEM = R"HTML(
var locations = {
  af:{af:["Abidjan","Algiers","Bissau","Cairo","Casablanca","El_Aaiun","Johannesburg","Juba","Khartoum","Lagos","Maputo","Monrovia","Nairobi","Ndjamena","Sao_Tome","Tripoli","Tunis","Windhoek"],at:["Cape_Verde"],in:["Mauritius"],},
  aq:["Casey","Davis","Macquarie","Mawson","Palmer","Rothera","Troll"],
  as:{as:["Almaty","Amman","Aqtau","Aqtobe","Ashgabat","Atyrau","Baghdad","Baku","Bangkok","Beirut","Bishkek","Choibalsan","Colombo","Damascus","Dhaka","Dili","Dubai","Dushanbe","Famagusta","Gaza","Hebron","Ho_Chi_Minh","Hong_Kong","Hovd","Jakarta","Jayapura","Jerusalem","Kabul","Karachi","Kathmandu","Kolkata","Kuching","Macau","Makassar","Manila","Nicosia","Oral","Pontianak","Pyongyang","Qatar","Qostanay","Qyzylorda","Riyadh","Samarkand","Seoul","Shanghai","Singapore","Taipei","Tashkent","Tbilisi","Tehran","Thimphu","Tokyo","Ulaanbaatar","Urumqi","Yangon","Yerevan"],in:["Chagos","Maldives"],},
  au:["Perth","Eucla","Adelaide","Broken_Hill","Darwin","Brisbane","Hobart","Lindeman","Melbourne","Sydney","Lord_Howe"],
  eu:{eu:["Andorra","Astrakhan","Athens","Belgrade","Berlin","Brussels","Bucharest","Budapest","Chisinau","Dublin","Gibraltar","Helsinki","Istanbul","Kaliningrad","Kirov","Kyiv","Lisbon","London","Madrid","Malta","Minsk","Moscow","Paris","Prague","Riga","Rome","Samara","Saratov","Simferopol","Sofia","Tallinn","Tirane","Ulyanovsk","Vienna","Vilnius","Volgograd","Warsaw","Zurich"],af:["Ceuta"],am:["Danmarkshavn","Nuuk","Scoresbysund","Thule"],as:["Anadyr","Barnaul","Chita","Irkutsk","Kamchatka","Khandyga","Krasnoyarsk","Magadan","Novokuznetsk","Novosibirsk","Omsk","Sakhalin","Srednekolymsk","Tomsk","Ust-Nera","Vladivostok","Yakutsk","Yekaterinburg"],at:["Azores","Canary","Faroe","Madeira"],},
  na:{am:["Adak","Anchorage","Bahia_Banderas","Barbados","Belize","Boise","Cambridge_Bay","Cancun","Chicago","Chihuahua","Ciudad_Juarez","Costa_Rica","Dawson","Dawson_Creek","Denver","Detroit","Edmonton","El_Salvador","Fort_Nelson","Glace_Bay","Goose_Bay","Grand_Turk","Guatemala","Halifax","Havana","Hermosillo","Indiana/Indianapolis","Indiana/Knox","Indiana/Marengo","Indiana/Petersburg","Indiana/Tell_City","Indiana/Vevay","Indiana/Vincennes","Indiana/Winamac","Inuvik","Iqaluit","Jamaica","Juneau","Kentucky/Louisville","Kentucky/Monticello","Los_Angeles","Managua","Martinique","Matamoros","Mazatlan","Menominee","Merida","Metlakatla","Mexico_City","Miquelon","Moncton","Monterrey","New_York","Nome","North_Dakota/Beulah","North_Dakota/Center","North_Dakota/New_Salem","Ojinaga","Panama","Phoenix","Port-au-Prince","Puerto_Rico","Rankin_Inlet","Regina","Resolute","Santo_Domingo","Sitka","St_Johns","Swift_Current","Tegucigalpa","Tijuana","Toronto","Vancouver","Whitehorse","Winnipeg","Yakutat"],pa:["Honolulu"],at:["Bermuda"],},
  sa:{am:["Araguaina","Argentina/Buenos_Aires","Argentina/Catamarca","Argentina/Cordoba","Argentina/Jujuy","Argentina/La_Rioja","Argentina/Mendoza","Argentina/Rio_Gallegos","Argentina/Salta","Argentina/San_Juan","Argentina/San_Luis","Argentina/Tucuman","Argentina/Ushuaia","Asuncion","Bahia","Belem","Boa_Vista","Bogota","Campo_Grande","Caracas","Cayenne","Cuiaba","Eirunepe","Fortaleza","Guayaquil","Guyana","La_Paz","Lima","Maceio","Manaus","Montevideo","Noronha","Paramaribo","Porto_Velho","Punta_Arenas","Recife","Rio_Branco","Santarem","Santiago","Sao_Paulo"],pa:["Easter","Galapagos"],aq:["Palmer"],at:["Stanley","South_Georgia"]},
  pa:["Apia","Auckland","Bougainville","Chatham","Efate","Fakaofo","Fiji","Gambier","Guadalcanal","Guam","Kanton","Kiritimati","Kosrae","Kwajalein","Marquesas","Nauru","Niue","Norfolk","Noumea","Pago_Pago","Palau","Pitcairn","Port_Moresby","Rarotonga","Tahiti","Tarawa","Tongatapu"],
  etc:["Greenwich","Universal","Zulu","GMT-14","GMT-13","GMT-12","GMT-11","GMT-10","GMT-9","GMT-8","GMT-7","GMT-6","GMT-5","GMT-4","GMT-3","GMT-2","GMT-1","GMT","GMT+1","GMT+2","GMT+3","GMT+4","GMT+5","GMT+6","GMT+7","GMT+8","GMT+9","GMT+10","GMT+11","GMT+12","UCT","UTC"]
};
var regions={as:"Asia",af:"Africa",eu:"Europe",na:"North America",sa:"South America",au:"Australia",pa:"Pacific",aq:"Antarctica",etc:"Etc"};
var prefix={am:"America",at:"Atlantic",in:"Indian",pa:"Pacific"};
var x = byID("tz");
var sel = x.getAttribute('data-option')||'Etc/UTC';
Object.entries(regions).forEach(entry => {
  const [k, v] = entry;
  var og = document.createElement("optgroup");
  og.label = (k in regions) ? regions[k]:v;
  x.add(og);
  Object.entries(locations[k]).sort().forEach(ls => {
    var [k1, l] = ls;
    if (typeof l == "string") {
      l = [l];
      k1 = k;
    }
    Object.values(l).sort().forEach(t => {
      var opt = document.createElement("option");
      var v = (k1 in prefix) ? prefix[k1]:(k1 in regions) ? regions[k1]:k1;
      opt.text = v + '/' + t;
      if (opt.text == sel) {
        opt.selected=true;
      }
      x.add(opt);
    });
  });
});
)HTML";
#endif

// Form HTML split into static (PROGMEM, zero-copy) and dynamic (current
// values) pieces. /ntpjs is NOT pulled in here - WebPortal triggers it
// lazily on <details ontoggle> so users who don't open the NTP section
// don't fetch the ~4 KB timezone payload.
void NTP_Client::renderInlineForm(EGHttpResponse& res) {
  res.sendKV(F("<form method=POST action=/ntpset>"
               "<label for=tz>Timezone</label>"
               "<select name=t id=tz data-option='"),
             get_tz(),
             F("'></select>"
               "<label for=ntps>NTP Server</label>"
               "<input id=ntps name=s value='"));
  res.sendKV(nullptr, get_server(), F("'><button type=submit>Save</button></form>"));
}

static void hNtpJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", "public, max-age=31536000, immutable");
#if EG_GZ_NTP_PAGE_JS
  res.sendGzipP(200, "application/javascript", NTP_PAGE_JS_GZ, NTP_PAGE_JS_GZ_LEN);
#else
  res.send(200, "application/javascript", FPSTR(NTP_PAGE_JS));
#endif
}

static const char NTP_SAVED_BODY[] PROGMEM =
  "<p>NTP settings saved. The new timezone and server are in effect now.</p>";

static void hNtpSet(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // arg() returns into a shared static decode buffer - call EGPrefs::put
  // (which copies internally) before the next arg() clobbers it.
  const char* s = req.arg("s");
  if (s) EGPrefs::put("ntp", "server", s);
  const char* t = req.arg("t");
  if (t) EGPrefs::put("ntp", "tz",     t);
  EGPrefs::commit();   // triggers NTP_Client::on_prefs_saved -> setup()

  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("NTP Saved"));
  res.sendChunk(FPSTR(NTP_SAVED_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

void NTP_Client::registerRoutes(EGHttpServer& http) {
  http.on("/ntpjs",  EGHttpRequest::GET,  hNtpJs);
  http.on("/ntpset", EGHttpRequest::POST, hNtpSet);
}
