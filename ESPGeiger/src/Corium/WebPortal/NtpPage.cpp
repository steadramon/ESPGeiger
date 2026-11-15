/*
  NtpPage.cpp - the Time / NTP block on /network, its JS and save.

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
#include <EGHttpServer.h>
#include <EGTimeZone.h>
#include <EGEscape.h>
#include "NtpPage.h"
#include "WebPortal.h"
#include "Corium/NTP/NTP.h"
#include "Corium/Prefs/Prefs.h"
#include "NTP_PAGE_JS.gz.h"

// No comments in here, it ships to the browser
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

void corium::NtpPage::form(EGHttpResponse& res) {
  // data-u 1 = the name isn't known
  char u[2] = { EGTimeZone::rule(ntpclient.getTz()) ? '0' : '1', '\0' };
  // One buffer, used for the tz then the server
  static_assert(NTP_TZ_MAX <= CORIUM_HOST_MAX, "esc too small for tz");
  char esc[6 * CORIUM_HOST_MAX + 1];
  res.sendKV(F("<form method=POST action=/ntpset>"
               "<label for=tz>Timezone</label>"
               "<select name=t id=tz data-u='"),
             u,
             F("' data-option='"));
  egesc_html(esc, sizeof(esc), ntpclient.getTz());
#ifdef DISABLE_NTP
  res.sendKV(nullptr, esc, F("'></select><button type=submit>Save</button></form>"));
#else
  res.sendKV(nullptr,
             esc,
             F("'></select>"
               "<label for=ntps>NTP server</label>"
               "<input id=ntps name=s maxlength=" STR(CORIUM_HOST_MAX) " value='"));
  egesc_html(esc, sizeof(esc), ntpclient.getServer());
  res.sendKV(nullptr, esc, F("'><button type=submit>Save</button></form>"));
#endif
}

static void hNtpJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_NTP_PAGE_JS
  corium::WebPortal::sendGz(res, FPSTR(HTTP_CT_JS), NTP_PAGE_JS_GZ, NTP_PAGE_JS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(NTP_PAGE_JS));
#endif
}

static const char NTP_SAVED_BODY[] PROGMEM =
  "<p>NTP settings saved. The new timezone and server are in effect now.</p>";
static const char NTP_REFUSED_BODY[] PROGMEM =
  "<div class='a ae'>Not all saved: the server name or timezone was not valid.</div>";

static void hNtpSet(EGHttpRequest& req, EGHttpResponse& res, void*) {
  bool ok = true;
  // arg() reuses its buffer so each one is used straight away, absent = not saved
#ifndef DISABLE_NTP
  const char* s = req.arg(F("s"));
  if (!s || !corium::Prefs::put("ntp", corium::K_SERVER, s)) ok = false;
#endif
  const char* t = req.arg(F("t"));
  if (!t || !EGTimeZone::rule(t) || !corium::Prefs::put("ntp", corium::K_TZ,  t)) ok = false;
  bool wrote = corium::Prefs::commit();

  corium::WebPortal::beginPage(res, ok && wrote ? F("NTP Saved") : F("NTP"));
  if (!wrote) corium::WebPortal::sendNotSaved(res);
  else res.sendChunk(ok ? FPSTR(NTP_SAVED_BODY) : FPSTR(NTP_REFUSED_BODY));
  corium::WebPortal::endPage(res);
}

void corium::NtpPage::routes(EGHttpServer& http) {
  http.on(F("/ntpjs"),  EGHttpRequest::GET,  hNtpJs);
  http.on(F("/ntpset"), EGHttpRequest::POST, hNtpSet);
}
