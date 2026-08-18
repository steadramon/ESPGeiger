/*
  EGPortal.cpp - Captive WiFi setup portal on AsyncTCP.

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

static const char P_ROOT[] PROGMEM = "/";

static const char PORTAL_HEAD[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>";

static const char PORTAL_STYLE[] PROGMEM =
  "</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
  "h1{font-size:1.4em;margin:0 0 .5em}"
  "label{display:block;margin:1em 0 .3em;font-size:.95em}"
  "input,select,button{width:100%;padding:.6em;font-size:1em;box-sizing:border-box;border:1px solid #bbb;border-radius:.3em}"
  "button{background:#06c;color:#fff;border:0;margin-top:1.2em;cursor:pointer}"
  "</style></head><body>"
  "<h1>";

static const char PORTAL_FORM[] PROGMEM =
  "<form action=/save method=post>"
  "<label>Network</label>"
  "<select onchange=\"document.getElementById('s').value=this.value\">"
  "<option value=''>- Pick a network -</option>";

static const char PORTAL_TAIL[] PROGMEM =
  "</select>"
  "<label>SSID <input id=s name=ssid maxlength=32 required></label>"
  "<label>Password <input name=password type=password maxlength=64></label>"
  "<button>Save</button></form>"
  "<form action=/rescan method=get style=margin-top:1em>"
  "<button type=submit style=background:#888>Rescan networks</button></form>";

static const char PORTAL_RESET_BTN[] PROGMEM =
  "<form action=/reset method=get style=margin-top:2.5em>"
  "<button type=submit style=\"background:#fff;color:#c0392b;border:1px solid #c0392b\">"
  "Factory reset&hellip;</button></form>";

static const char PORTAL_END[] PROGMEM = "</body></html>";

static const char RESET_CONFIRM_HTML[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>Factory reset</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
  "h1{font-size:1.4em;color:#c0392b}"
  "button,a.c{width:100%;padding:.6em;font-size:1em;box-sizing:border-box;border-radius:.3em;display:block;text-align:center}"
  "button{background:#c0392b;color:#fff;border:0;cursor:pointer}"
  "a.c{margin-top:1em;background:#eee;color:#333;text-decoration:none;border:1px solid #bbb}"
  "</style></head><body>"
  "<h1>Factory reset</h1>"
  "<p>This erases <b>all settings</b>: WiFi credentials, module configuration "
  "and calibration. The device restarts into this setup portal as if new.</p>"
  "<form action=/reset_go method=post><button>Erase everything</button></form>"
  "<a class=c href=/>Cancel</a>"
  "</body></html>";

static const char RESET_DONE_HTML[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>Resetting</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:3em auto;padding:0 1em;text-align:center}"
  "</style></head><body>"
  "<h1>Erasing&hellip;</h1>"
  "<p>All settings cleared. The device is restarting and will reopen "
  "the setup AP in a few seconds.</p></body></html>";

static const char RESCAN_HTML[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta http-equiv=refresh content=\"4;url=/\">"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>Scanning</title><style>"
  "body{font-family:sans-serif;text-align:center;margin-top:4em}"
  "</style></head><body><h1>Scanning&hellip;</h1>"
  "<p>This page reloads in a few seconds.</p></body></html>";

static const char SAVED_HTML[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>Saved</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:3em auto;padding:0 1em;text-align:center}"
  "</style></head><body>"
  "<h1>Saved</h1><p>Connecting to your network. The setup AP will close shortly.</p>"
  "</body></html>";

void EGPortal::begin(const char* apSsid, const char* apPassword) {
  if (_active) return;

  // STA only, AP_STA scan is unreliable
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks(false);
  Serial.printf("[EGPortal] sync scan: %d networks\n", n);
  refreshScan(n);
  WiFi.scanDelete();

#if defined(ESP8266)
  WiFi.mode(WIFI_AP_STA);
#elif defined(ESP32)
  WiFi.mode(WIFI_MODE_APSTA);
#endif
  if (apPassword && apPassword[0]) WiFi.softAP(apSsid, apPassword);
  else                              WiFi.softAP(apSsid);

  IPAddress apIp = WiFi.softAPIP();
  _dns.setErrorReplyCode(DNSReplyCode::NoError);
  _dns.start(53, "*", apIp);

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
  _http.begin(80);

  _active = true;
}

// loop() collects it
void EGPortal::kickScan() {
  WiFi.scanNetworks(true);
}

void EGPortal::takeScan(int n) {
  Serial.printf("[EGPortal] async scan: %d networks\n", n);
  if (n > 0) refreshScan(n);
  WiFi.scanDelete();
}

void EGPortal::loop() {
  if (!_active) return;
  _dns.processNextRequest();
  _http.tick();
  int n = WiFi.scanComplete();
  if (n >= 0) takeScan(n);
}

void EGPortal::end() {
  if (!_active) return;
  _http.end();
  _dns.stop();
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
static bool scan_ssid(int i, char* out) {
#ifdef ESP32
  const wifi_ap_record_t* r = (const wifi_ap_record_t*)WiFi.getScanInfoByIndex(i);
  if (!r) return false;
  memcpy(out, r->ssid, EGPORTAL_SSID_MAX);
  out[EGPORTAL_SSID_MAX] = '\0';
#else
  const bss_info* r = WiFi.getScanInfoByIndex(i);
  if (!r) return false;
  size_t n = r->ssid_len < EGPORTAL_SSID_MAX ? r->ssid_len : EGPORTAL_SSID_MAX;
  memcpy(out, r->ssid, n);
  out[n] = '\0';
#endif
  return out[0] != '\0';
}

size_t EGPortal::processScan(ScanEntry* out, size_t maxOut, int rawCount,
                              int8_t rssiMin) {
  if (rawCount <= 0 || maxOut == 0 || !out) return 0;
  size_t count = 0;
  char s[EGPORTAL_SSID_MAX + 1];
  for (int i = 0; i < rawCount; i++) {
    int8_t rssi = (int8_t)WiFi.RSSI(i);
    if (rssi < rssiMin) continue;
    if (!scan_ssid(i, s)) continue;

    // keep strongest
    int existing = find_ssid(out, count, s);
    if (existing >= 0) {
      if (rssi > out[existing].rssi) out[existing].rssi = rssi;
      continue;
    }
    if (count >= maxOut) continue;

    ScanEntry& e = out[count++];
    memcpy(e.ssid, s, sizeof(s));
    e.rssi = rssi;
    e.enc  = (uint8_t)WiFi.encryptionType(i);
  }
  sort_by_rssi(out, count);
  return count;
}

void EGPortal::refreshScan(int n) {
  Serial.printf("[EGPortal] refreshScan(%d)\n", n);
  _scanCount = (uint8_t)processScan(_scan, EGPORTAL_SCAN_MAX, n, EGPORTAL_RSSI_MIN);
}

void EGPortal::hRoot(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);

  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  res.sendChunk(FPSTR(PORTAL_HEAD));
  res.sendChunk(self->_title);
  res.sendChunk(FPSTR(PORTAL_STYLE));
  res.sendChunk(self->_title);
  res.sendChunk(F("</h1>"));
  if (self->_notice) res.sendChunk(self->_notice);
  res.sendChunk(FPSTR(PORTAL_FORM));

  char ssid[6 * EGPORTAL_SSID_MAX + 1];
  char opt[2 * sizeof(ssid) + 40];
  for (uint8_t i = 0; i < self->_scanCount; i++) {
    egesc_html(ssid, sizeof(ssid), self->_scan[i].ssid);
    int m = snprintf_P(opt, sizeof(opt),
                       PSTR("<option value='%s'>%s (%d dBm)</option>"),
                       ssid, ssid, (int)self->_scan[i].rssi);
    if (m > 0 && (size_t)m < sizeof(opt)) res.sendChunk(opt, (size_t)m);
  }

  res.sendChunk(FPSTR(PORTAL_TAIL));
  if (self->_onReset) res.sendChunk(FPSTR(PORTAL_RESET_BTN));
  res.sendChunk(FPSTR(PORTAL_END));
  res.endChunked();
}

void EGPortal::hScan(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  Serial.printf_P(PSTR("[EGPortal] /scan hit, cache=%u\n"), self->_scanCount);
  if (self->_scanCount == 0 && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    self->kickScan();
  }
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(F("["));
  char ssid[6 * EGPORTAL_SSID_MAX + 1];
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

void EGPortal::hSave(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);

  const char* ssid = req.arg(F("ssid"));
  if (!ssid || !ssid[0]) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("ssid required"));
    return;
  }
  // a cut one would fail to join
  if (strlen(ssid) > EGPORTAL_SSID_MAX) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("ssid too long"));
    return;
  }
  // arg() buffer is shared
  strncpy(self->_savedSsid, ssid, EGPORTAL_SSID_MAX);
  self->_savedSsid[EGPORTAL_SSID_MAX] = '\0';

  const char* pass = req.arg(F("password"));
  if (pass && strlen(pass) > EGPORTAL_PASS_MAX) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("password too long"));
    return;
  }
  if (pass) {
    strncpy(self->_savedPass, pass, EGPORTAL_PASS_MAX);
    self->_savedPass[EGPORTAL_PASS_MAX] = '\0';
  } else {
    self->_savedPass[0] = '\0';
  }

  res.send(200, FPSTR(HTTP_CT_HTML), FPSTR(SAVED_HTML));

  if (self->_onSave) self->_onSave(self->_savedSsid, self->_savedPass, self->_onSaveUser);
}

void EGPortal::hRescan(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  self->kickScan();
  res.send(200, FPSTR(HTTP_CT_HTML), FPSTR(RESCAN_HTML));
}

void EGPortal::hResetConfirm(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  if (!self->_onReset) { res.redirect(FPSTR(P_ROOT)); return; }
  res.send(200, FPSTR(HTTP_CT_HTML), FPSTR(RESET_CONFIRM_HTML));
}

void EGPortal::hResetGo(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  if (!self->_onReset) { res.redirect(FPSTR(P_ROOT)); return; }
  res.send(200, FPSTR(HTTP_CT_HTML), FPSTR(RESET_DONE_HTML));
  self->_onReset(self->_onResetUser);
}

void EGPortal::hCaptive(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // absolute, for the captive popup
  IPAddress ip = WiFi.softAPIP();
  char loc[40];
  snprintf_P(loc, sizeof(loc), PSTR("http://%u.%u.%u.%u/"), ip[0], ip[1], ip[2], ip[3]);
  res.redirect(loc);
}
