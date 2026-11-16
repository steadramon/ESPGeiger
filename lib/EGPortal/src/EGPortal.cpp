/*
  EGPortal.cpp - Captive WiFi setup portal on EGTcp.

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
#include "EGPortal.h"
#include <EGEscape.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#endif

#define EGP_STR_(x) #x
#define EGP_STR(x)  EGP_STR_(x)

static const char P_ROOT[] PROGMEM = "/";

// leaves <style> open, each page adds its own
static const char PAGE_TOP[] PROGMEM =
  "<!doctype html><html lang=en><head><meta charset=utf-8>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}";

static const char PORTAL_STYLE[] PROGMEM =
  "h1{font-size:1.4em;margin:0 0 .5em}"
  "label{display:block;margin:1em 0 .3em;font-size:.95em}"
  "input,select,button{width:100%;padding:.6em;font-size:1em;box-sizing:border-box;border:1px solid #bbb;border-radius:.3em}"
  "button{background:#06c;color:#fff;border:0;margin-top:1.2em;cursor:pointer}"
  "</style><title>";

static const char PORTAL_FORM[] PROGMEM =
  "<form action=/save method=post>"
  "<label for=n>Network</label>"
  "<select id=n onchange=\"document.getElementById('s').value=this.value\">"
  "<option value=''>- Pick a network -</option>";

static const char PORTAL_TAIL[] PROGMEM =
  "</select>"
  "<label>SSID <input id=s name=ssid maxlength=" EGP_STR(EGPORTAL_SSID_MAX) " required></label>"
  "<label>Password <input name=password type=password maxlength=" EGP_STR(EGPORTAL_PASS_MAX) "></label>"
  "<button>Save</button></form>"
  "<form action=/rescan method=get style=margin-top:1em>"
  "<button type=submit style=background:#666>Rescan networks</button></form>";

static const char PORTAL_RESET_BTN[] PROGMEM =
  "<form action=/reset method=get style=margin-top:2.5em>"
  "<button type=submit style=\"background:#fff;color:#c0392b;border:1px solid #c0392b\">"
  "Factory reset&hellip;</button></form>";

static const char PORTAL_END[] PROGMEM = "</body></html>";

// after PAGE_TOP
static const char RESET_CONFIRM_HTML[] PROGMEM =
  "h1{font-size:1.4em;color:#c0392b}"
  "button,a.c{width:100%;padding:.6em;font-size:1em;box-sizing:border-box;border-radius:.3em;display:block;text-align:center}"
  "button{background:#c0392b;color:#fff;border:0;cursor:pointer}"
  "a.c{margin-top:1em;background:#eee;color:#333;text-decoration:none;border:1px solid #bbb}"
  "</style><title>Factory reset</title></head><body>"
  "<h1>Factory reset</h1>"
  "<p>This erases <b>all settings</b>: WiFi credentials, module configuration "
  "and calibration. The device restarts into this setup portal as if new.</p>"
  "<form action=/reset_go method=post><button>Factory reset</button></form>"
  "<a class=c href=/>Cancel</a>"
  "</body></html>";

static const char RESET_DONE_HTML[] PROGMEM =
  "body{margin-top:3em;text-align:center}"
  "</style><title>Factory reset</title></head><body>"
  "<h1>Factory reset</h1>"
  "<p>All settings cleared. The device is restarting and will reopen "
  "the setup portal in a few seconds.</p></body></html>";

static const char SAVED_HTML[] PROGMEM =
  "body{margin-top:3em;text-align:center}"
  "</style><title>Saved</title></head><body>"
  "<h1>Saved</h1><p>Restarting to join <b>";

static const char SAVED_END[] PROGMEM =
  "</b>. If it can't, this setup hotspot comes back.</p></body></html>";

static const char RESCAN_HTML[] PROGMEM =
  "body{margin-top:4em;text-align:center}"
  "</style><meta http-equiv=refresh content=\"4;url=/\">"
  "<title>Scanning</title></head><body><h1>Scanning&hellip;</h1>"
  "<p>This page reloads in a few seconds.</p></body></html>";

bool EGPortal::begin(const char* apSsid, const char* apPassword) {
  if (_active) return true;

  // STA only, AP_STA scan is unreliable
  WiFi.mode(WIFI_STA);
  (void)WiFi.disconnect(false, false);   // keep the creds, one arg wipes them; only for the scan
  delay(100);
  int n = WiFi.scanNetworks(false);
  refreshScan(n);
  WiFi.scanDelete();

#if defined(ESP8266)
  WiFi.mode(WIFI_AP_STA);
#elif defined(ESP32)
  WiFi.mode(WIFI_MODE_APSTA);
#endif
  // the STA's join moves the AP to its channel, start on the likely one
  uint8_t chan = (_scanCount && _scan[0].chan) ? _scan[0].chan : 1;
  bool up = WiFi.softAP(apSsid, (apPassword && apPassword[0]) ? apPassword : nullptr, chan);

  IPAddress apIp = WiFi.softAPIP();
  up = _dns.begin(53, EGTcpAddr(apIp[0], apIp[1], apIp[2], apIp[3])) && up;

  // routes outlive end()
  if (!_routed) {
    _http.on(FPSTR(P_ROOT),  EGHttpRequest::GET,  &EGPortal::hRoot, this);
    _http.on(F("/scan"),     EGHttpRequest::GET,  &EGPortal::hScan, this);
    _http.on(F("/rescan"),   EGHttpRequest::GET,  &EGPortal::hRescan, this);
    _http.on(F("/save"),     EGHttpRequest::POST, &EGPortal::hSave, this);
    _http.on(F("/reset"),    EGHttpRequest::GET,  &EGPortal::hResetConfirm, this);
    _http.on(F("/reset_go"), EGHttpRequest::POST, &EGPortal::hResetGo, this);
    _http.onNotFound(&EGPortal::hCaptive, this);
    _routed = true;
  }
  _http.begin(80);   // retries its own listen

  _active = true;
  return up;
}

void EGPortal::kickScan() {
  WiFi.scanNetworks(true);
}

bool EGPortal::heard(const char* ssid) const {
  for (uint8_t i = 0; i < _scanCount; i++) {
    if (strcmp(_scan[i].ssid, ssid) == 0) return true;
  }
  return false;
}

void EGPortal::takeScan(int n) {
  if (n > 0) refreshScan(n);
  WiFi.scanDelete();
}

void EGPortal::loop() {
  if (!_active) return;
  _dns.poll();
  EGTcp::poll();   // runs in setup, before loop()
  _http.tick();
  int n = WiFi.scanComplete();
  if (n >= 0) takeScan(n);
}

bool EGPortal::busy() const {
  return _http.tickWanted() || EGTcp::pending.load(std::memory_order_relaxed) || _dns.waiting();
}

void EGPortal::end() {
  if (!_active) return;
  _http.end();
  _dns.end();
  WiFi.softAPdisconnect(true);
  _active = false;
}

void EGPortal::onSave(SaveCallback cb, void* user) {
  _onSave = cb;
  _onSaveUser = user;
}

void EGPortal::onReset(ResetCallback cb, void* user) {
  _onReset = cb;
  _onResetUser = user;
}

static int find_ssid(const EGPortal::ScanEntry* out, size_t count, const char* ssid) {
  for (size_t j = 0; j < count; j++) {
    if (strcmp(out[j].ssid, ssid) == 0) return (int)j;
  }
  return -1;
}

static size_t weakest(const EGPortal::ScanEntry* out, size_t count) {
  size_t at = 0;
  for (size_t j = 1; j < count; j++) {
    if (out[j].rssi < out[at].rssi) at = j;
  }
  return at;
}

// strongest first
static void sort_by_rssi(EGPortal::ScanEntry* out, size_t count) {
  for (size_t i = 1; i < count; i++) {
    EGPortal::ScanEntry cur = out[i];
    int j = (int)i;
    while (j > 0 && out[j - 1].rssi < cur.rssi) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = cur;
  }
}

// a 32 byte SSID has no NUL
static bool scan_ssid(int i, char* out, uint8_t* chan) {
#ifdef ESP32
  const wifi_ap_record_t* r = (const wifi_ap_record_t*)WiFi.getScanInfoByIndex(i);
  if (!r) return false;
  memcpy(out, r->ssid, EGPORTAL_SSID_MAX);
  out[EGPORTAL_SSID_MAX] = '\0';
  *chan = r->primary;
#else
  const bss_info* r = WiFi.getScanInfoByIndex(i);
  if (!r) return false;
  size_t n = r->ssid_len < EGPORTAL_SSID_MAX ? r->ssid_len : EGPORTAL_SSID_MAX;
  memcpy(out, r->ssid, n);
  out[n] = '\0';
  *chan = r->channel;
#endif
  return out[0] != '\0';
}

size_t EGPortal::processScan(ScanEntry* out, size_t maxOut, int rawCount,
                              int8_t rssiMin) {
  if (rawCount <= 0 || maxOut == 0 || !out) return 0;
  size_t count = 0;
  char s[EGPORTAL_SSID_MAX + 1];
  uint8_t chan = 0;
  for (int i = 0; i < rawCount; i++) {
    int8_t rssi = (int8_t)WiFi.RSSI(i);
    if (rssi < rssiMin) continue;
    if (!scan_ssid(i, s, &chan)) continue;

    // keep strongest
    int existing = find_ssid(out, count, s);
    if (existing >= 0) {
      if (rssi > out[existing].rssi) {
        out[existing].rssi = rssi;
        out[existing].enc  = (uint8_t)WiFi.encryptionType(i);
        out[existing].chan = chan;
      }
      continue;
    }
    // full: the weakest goes, a tie stays
    size_t at = count;
    if (count >= maxOut) {
      at = weakest(out, count);
      if (rssi <= out[at].rssi) continue;
    } else {
      count++;
    }

    ScanEntry& e = out[at];
    memcpy(e.ssid, s, sizeof(s));
    e.rssi = rssi;
    e.enc  = (uint8_t)WiFi.encryptionType(i);
    e.chan = chan;
  }
  sort_by_rssi(out, count);
  return count;
}

void EGPortal::refreshScan(int n) {
  _scanCount = (uint8_t)processScan(_scan, EGPORTAL_SCAN_MAX, n, EGPORTAL_RSSI_MIN);
}

static void send_page(EGHttpResponse& res, PGM_P html) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  res.sendChunk(FPSTR(PAGE_TOP));
  res.sendChunk(FPSTR(html));
  res.endChunked();
}

// to the notice, as the root page
static void send_head(EGHttpResponse& res, const char* title, const char* notice) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  res.sendChunk(FPSTR(PAGE_TOP));
  res.sendChunk(FPSTR(PORTAL_STYLE));
  res.sendChunk(title);
  res.sendChunk(F("</title></head><body><h1>"));
  res.sendChunk(title);
  res.sendChunk(F("</h1>"));
  if (notice) res.sendChunk(notice);
}

void EGPortal::hRoot(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);

  send_head(res, self->_title, self->_notice);
  res.sendChunk(FPSTR(PORTAL_FORM));

  char ssid[EGESC_GROW_MAX * EGPORTAL_SSID_MAX + 1];
  char opt[2 * sizeof(ssid) + 40];
  for (uint8_t i = 0; i < self->_scanCount; i++) {
    egesc_html(ssid, sizeof(ssid), self->_scan[i].ssid);
    int n = snprintf_P(opt, sizeof(opt),
                       PSTR("<option value='%s'>%s (%d dBm)</option>"),
                       ssid, ssid, (int)self->_scan[i].rssi);
    if (n > 0 && (size_t)n < sizeof(opt)) res.sendChunk(opt, (size_t)n);
  }

  res.sendChunk(FPSTR(PORTAL_TAIL));
  if (self->_onReset) res.sendChunk(FPSTR(PORTAL_RESET_BTN));
  res.sendChunk(FPSTR(PORTAL_END));
  res.endChunked();
}

void EGPortal::hScan(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  if (self->_scanCount == 0 && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    self->kickScan();
  }
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(F("["));
  char ssid[EGESC_GROW_MAX * EGPORTAL_SSID_MAX + 1];
  char row[sizeof(ssid) + 24];
  for (uint8_t i = 0; i < self->_scanCount; i++) {
    egesc_json(ssid, sizeof(ssid), self->_scan[i].ssid);
    int n = snprintf_P(row, sizeof(row),
                       PSTR("%s{\"s\":\"%s\",\"r\":%d}"),
                       i ? PSTR(",") : PSTR(""),
                       ssid, (int)self->_scan[i].rssi);
    if (n > 0 && (size_t)n < sizeof(row)) res.sendChunk(row, (size_t)n);
  }
  res.sendChunk(F("]"));
  res.endChunked();
}

static const char CREDS_T_SSID[] PROGMEM = "ssid must be 1-32 characters";
static const char CREDS_T_LONG[] PROGMEM = "password too long";
static const char CREDS_T_PASS[] PROGMEM = "password must be empty (open), 8-63 characters or 64 hex digits";
static const char* const CREDS_TEXT[] PROGMEM = {CREDS_T_SSID, CREDS_T_LONG, CREDS_T_PASS};

void EGPortal::hSave(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  char ssid[EGPORTAL_SSID_MAX + 1], pass[EGPORTAL_PASS_MAX + 1];
  Creds c = takeCreds(req, ssid, pass);
  if (c != CREDS_OK) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), FPSTR((PGM_P)pgm_read_ptr(&CREDS_TEXT[c - 1])));
    return;
  }

  if (self->_onSave && !self->_onSave(ssid, pass, self->_onSaveUser)) {
    res.send(500, FPSTR(HTTP_CT_PLAIN), F("WiFi settings not saved: out of memory"));
    return;
  }

  char s[EGESC_GROW_MAX * EGPORTAL_SSID_MAX + 1];
  egesc_html(s, sizeof(s), ssid);
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  res.sendChunk(FPSTR(PAGE_TOP));
  res.sendChunk(FPSTR(SAVED_HTML));
  res.sendKV(nullptr, s, FPSTR(SAVED_END));
  res.endChunked();
}

void EGPortal::hRescan(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  self->kickScan();
  send_page(res, RESCAN_HTML);
}

void EGPortal::hResetConfirm(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  if (!self->_onReset) { res.redirect(FPSTR(P_ROOT)); return; }
  send_page(res, RESET_CONFIRM_HTML);
}

void EGPortal::hResetGo(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  if (!self->_onReset) { res.redirect(FPSTR(P_ROOT)); return; }
  send_page(res, RESET_DONE_HTML);
  self->_onReset(self->_onResetUser);
}

void EGPortal::hCaptive(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // absolute, for the captive popup
  IPAddress ip = WiFi.softAPIP();
  char loc[40];
  snprintf_P(loc, sizeof(loc), PSTR("http://%u.%u.%u.%u/"), ip[0], ip[1], ip[2], ip[3]);
  res.redirect(loc);
}
