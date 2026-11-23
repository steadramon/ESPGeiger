/*
  WebAPIWeb.cpp - the ESPGeiger Network pages: /webapi, key reset, forget.

  Copyright (C) 2026 @steadramon

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
#ifdef WEBAPIOUT

#include <EGHttpServer.h>
#include "WebAPI.h"
#include "WebAPIKey.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Prefs/Prefs.h"
#include "App/AppKeys.h"
#include "Corium/Util/Words.h"
#include "Corium/WebPortal/WebPortal.h"

static const corium::MenuEntry WEBAPI_MENU[] PROGMEM = {
  {"/webapi", "ESPGeiger Stations"},
  {"", ""}
};

static const char FORGET_BODY[] PROGMEM =
  "<p>A delete request goes to the server as soon as it can be reached. Sharing is now <b>Off</b>.</p>"
  "<p class=muted>Re-enabling sharing later will register a new station; old readings will not be linked.</p>";

// false if a field was refused, the rest are still saved
static bool save_settings(EGHttpRequest& req, bool& wrote) {
  bool ok = true;
  // arg() reuses its buffer
  const char* m = req.arg(F("m"));
  if (m && !corium::Prefs::put("webapi", EG_K_MODE, m)) ok = false;
  char lat[24] = {};
  const char* a = req.arg(FPSTR(corium::K_LAT));
  if (a) strlcpy(lat, a, sizeof(lat));
  const char* lon = req.arg(FPSTR(corium::K_LON));
  // Lat and lon both or neither, 0,0 means use the IP location
  if (a && lon && !*lat != !*lon) {
    ok = false;
  } else {
    if (a && !corium::Prefs::put("sys", corium::K_LAT, *lat ? lat : "0")) ok = false;
    if (lon && !corium::Prefs::put("sys", corium::K_LON, *lon ? lon : "0")) ok = false;
  }
  wrote = corium::Prefs::commit();
  return ok;
}

static void send_station(EGHttpResponse& res) {
  if (webapi.getStationId() != 0) {
    corium::WebPortal::sendf(res,
      PSTR("<p>Station: <a href='" WEBAPI_STATION_URL "' target=_blank>#%u</a></p>"),
      webapi.getStationId(), webapi.getStationId());
  } else {
    res.sendChunk(F(
      "<p>Station: not yet registered (handshake pending) "
      "<button type=button onclick='location.reload()'>Refresh</button></p>"));
  }
}

static void send_sharing(EGHttpResponse& res, uint8_t mode) {
  // 0,0 shows blank, the placeholder says why
  bool ip = corium::Prefs::getFloat("sys", corium::K_LAT) == 0.0f && corium::Prefs::getFloat("sys", corium::K_LON) == 0.0f;
  res.sendChunk(F(
    "<form method=POST action=/webapi><fieldset><legend>Sharing</legend>"
    "<label><input type=radio name=m value=0"));
  if (mode == 0) res.sendChunk(FPSTR(W_CHECKED));
  res.sendChunk(F("> Off</label>"
    "<label><input type=radio name=m value=1"));
  if (mode == 1) res.sendChunk(FPSTR(W_CHECKED));
  res.sendChunk(F("> Heartbeat</label>"
    "<label><input type=radio name=m value=2"));
  if (mode == 2) res.sendChunk(FPSTR(W_CHECKED));
  res.sendKV(F("> CPM readings</label></fieldset>"
               "<label for=lat>Latitude</label>"
               "<input id=lat name=lat type=number step=any min=-90 max=90 placeholder='approximate by IP if blank' value='"),
             ip ? "" : corium::Prefs::getString("sys", corium::K_LAT),
             F("'><label for=lon>Longitude</label>"
               "<input id=lon name=lon type=number step=any min=-180 max=180 placeholder='approximate by IP if blank' value='"));
  res.sendKV(nullptr, ip ? "" : corium::Prefs::getString("sys", corium::K_LON),
             F("'><p style=margin-top:.5em>"));
  res.sendChunk(F(
    "<button type=button onclick=\"window.open('" APP_LOC_URL "/','" APP_LOC_WIN "')\">Find location</button></p>"
    "<button type=submit>Save</button></form>"
    "<script>window.addEventListener('message',function(e){if(e.origin!=='" APP_LOC_URL "')return;var d=e.data;if(!d||d.type!=='" APP_LOC_MSG "')return;var la=document.getElementById('lat');var lo=document.getElementById('lon');if(la&&typeof d.lat==='number')la.value=d.lat;if(lo&&typeof d.lon==='number')lo.value=d.lon;});</script>"));
}

static void send_advanced(EGHttpResponse& res, uint8_t mode) {
  res.sendChunk(F(
    "<details style=margin-top:1em><summary>Advanced</summary>"
    "<p class=muted>Reset the station key. The next handshake registers a new station: the existing one <b>cannot be retrieved</b>.</p>"
    "<form method=POST action=/webapikeyreset onsubmit=\"return confirm('Reset the station key? This orphans the existing station.')\">"
    "<button type=submit class=danger>Reset key</button></form>"));
  if (mode > 0 && webapi.getStationId() != 0) {
    res.sendChunk(F(
      "<p class=muted style=margin-top:1em>Delete this station from the network. Removes all readings and history server-side and switches sharing to Off. <b>Cannot be undone.</b></p>"
      "<form method=POST action=/webapiforget onsubmit=\"return confirm('Permanently delete this station from the network? This cannot be undone.')\">"
      "<button type=submit class=danger>Forget station</button></form>"));
  }
  res.sendChunk(FPSTR(W_DETAILS_END));
}

// A POST saves first, then both show the page
static void hWebAPI(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const bool posted = req.method() == EGHttpRequest::POST;
  bool wrote = true;
  const bool saved = posted && save_settings(req, wrote);

  corium::WebPortal::beginPage(res, FPSTR(WEBAPI_MENU[0].label));
  if (!wrote) corium::WebPortal::sendNotSaved(res);
  else if (saved) res.sendChunk(F("<div class='a ak'>Saved.</div>"));
  else if (posted) res.sendChunk(F("<div class='a ae'>A value was out of range and not saved.</div>"));
  res.sendChunk(F(
    "<p class=muted>ESPGeiger can share data with ESPGeiger Stations "
    "so it appears on the global map. The device identity and "
    "location are obscured before publication.</p>"));

  uint8_t mode = (uint8_t)corium::Prefs::getUInt("webapi", EG_K_MODE);
  if (mode > 0) send_station(res);

  send_sharing(res, mode);
  send_advanced(res, mode);

  corium::WebPortal::endPage(res);
}

static void hKeyReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::Log::console(PSTR("WebAPI: Resetting api.key"));
  bool gone = forget_key();
  corium::WebPortal::beginPage(res, F("Station key reset"));
  if (!gone) {
    corium::WebPortal::sendNotSaved(res);
    corium::WebPortal::endPage(res);
    return;
  }
  res.sendChunk(F("<p>Station key cleared. Device is restarting; a new station identity will be created on next connection.</p>"));
  corium::WebPortal::endRestartPage(res);
}

static void hForget(EGHttpRequest& req, EGHttpResponse& res, void*) {
  webapi.forget();
  bool wrote = corium::Prefs::put("webapi", EG_K_MODE, "0") && corium::Prefs::commit();

  corium::WebPortal::beginPage(res, F("Station forgotten"));
  if (!wrote) corium::WebPortal::sendNotSaved(res);
  res.sendChunk(FPSTR(FORGET_BODY));
  corium::WebPortal::endPage(res);
}

const corium::MenuEntry* WebAPI::menuEntries() { return WEBAPI_MENU; }

static const char R_WEBAPI[] PROGMEM = "/webapi";
static const char R_KEYRESET[] PROGMEM = "/webapikeyreset";
static const char R_FORGET[]   PROGMEM = "/webapiforget";

static const corium::WebRoute ROUTES[] PROGMEM = {
  {R_WEBAPI,   hWebAPI,   EGHttpRequest::GET},
  {R_WEBAPI,   hWebAPI,   EGHttpRequest::POST},
  {R_KEYRESET, hKeyReset, EGHttpRequest::POST},
  {R_FORGET,   hForget,   EGHttpRequest::POST},
};

void WebAPI::registerRoutes(EGHttpServer& http) {
  corium::WebPortal::on(http, ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
}
#endif
