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

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#endif

#define SCAN_REFRESH_MS 30000UL
#define SCAN_RSSI_MIN   -85    // dBm; weaker than this is unusable in practice

static const char PORTAL_HEAD[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>%s</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
  "h1{font-size:1.4em;margin:0 0 .5em}"
  "label{display:block;margin:1em 0 .3em;font-size:.95em}"
  "input,select,button{width:100%%;padding:.6em;font-size:1em;box-sizing:border-box;border:1px solid #bbb;border-radius:.3em}"
  "button{background:#06c;color:#fff;border:0;margin-top:1.2em;cursor:pointer}"
  "</style></head><body>"
  "<h1>%s</h1>"
  "%s"  // optional notice
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
  "<button type=submit style=background:#888>Rescan networks</button></form>"
  "</body></html>";

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

  // Scan first, in STA-only mode - far more reliable than AP_STA scan.
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

  _http.on("/",         EGHttpRequest::GET,  &EGPortal::hRoot, this);
  _http.on("/scan",     EGHttpRequest::GET,  &EGPortal::hScan, this);
  _http.on("/rescan",   EGHttpRequest::GET,  &EGPortal::hRescan, this);
  _http.on("/save",     EGHttpRequest::POST, &EGPortal::hSave, this);
  _http.onNotFound(&EGPortal::hCaptive, this);
  _http.begin(80);

  _scanLast = millis();
  _active = true;
}

void EGPortal::kickScan() {
#if defined(ESP8266)
  WiFi.scanNetworksAsync([this](int n) {
    Serial.printf("[EGPortal] async scan: %d networks\n", n);
    if (n > 0) refreshScan(n);
    WiFi.scanDelete();
    _scanLast = millis();
  });
#else
  WiFi.scanNetworks(true);
#endif
}

void EGPortal::loop() {
  if (!_active) return;
  _dns.processNextRequest();
  _http.tick();
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

size_t EGPortal::processScan(ScanEntry* out, size_t maxOut, int rawCount,
                              int8_t rssiMin) {
  if (rawCount <= 0 || maxOut == 0 || !out) return 0;
  size_t count = 0;
  for (int i = 0; i < rawCount; i++) {
    int8_t rssi = (int8_t)WiFi.RSSI(i);
    if (rssi < rssiMin) continue;     // cheap filter before String alloc
    String s = WiFi.SSID(i);
    if (!s.length()) continue;

    // Dedup by SSID - keep strongest. Linear scan is fine at this size.
    int existing = -1;
    for (size_t j = 0; j < count; j++) {
      if (strcmp(out[j].ssid, s.c_str()) == 0) { existing = j; break; }
    }
    if (existing >= 0) {
      if (rssi > out[existing].rssi) out[existing].rssi = rssi;
      continue;
    }
    if (count >= maxOut) continue;

    ScanEntry& e = out[count++];
    strncpy(e.ssid, s.c_str(), EGPORTAL_SSID_MAX);
    e.ssid[EGPORTAL_SSID_MAX] = '\0';
    e.rssi = rssi;
    e.enc  = (uint8_t)WiFi.encryptionType(i);
  }

  // Insertion sort by RSSI descending (strongest first).
  for (size_t i = 1; i < count; i++) {
    ScanEntry cur = out[i];
    int j = (int)i;
    while (j > 0 && out[j - 1].rssi < cur.rssi) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = cur;
  }
  return count;
}

void EGPortal::refreshScan(int n) {
  Serial.printf("[EGPortal] refreshScan(%d)\n", n);
  _scanCount = (uint8_t)processScan(_scan, EGPORTAL_SCAN_MAX, n, SCAN_RSSI_MIN);
}

// ---- handlers ----

void EGPortal::hRoot(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);

  // Stream chunked: PORTAL_HEAD is ~700 B PROGMEM with three %s slots
  // (title x2 + optional notice). Render expanded into a one-shot stack
  // buffer big enough for any plausible title/notice combination.
  res.beginChunked(200, "text/html");
  char head[1024];
  int n = snprintf_P(head, sizeof(head), PORTAL_HEAD,
                     self->_title, self->_title,
                     self->_notice ? self->_notice : "");
  if (n > 0 && (size_t)n < sizeof(head)) res.sendChunk(head, (size_t)n);

  char opt[128];
  for (uint8_t i = 0; i < self->_scanCount; i++) {
    int m = snprintf(opt, sizeof(opt),
                     "<option value='%s'>%s (%d dBm)</option>",
                     self->_scan[i].ssid, self->_scan[i].ssid,
                     (int)self->_scan[i].rssi);
    if (m > 0) res.sendChunk(opt, (size_t)m);
  }

  res.sendChunk(FPSTR(PORTAL_TAIL));
  res.endChunked();
}

void EGPortal::hScan(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  Serial.printf("[EGPortal] /scan hit, cache=%u\n", self->_scanCount);
  // If cache empty and no scan in flight, kick one. JS polls until populated.
  if (self->_scanCount == 0 && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    self->kickScan();
  }
  res.beginChunked(200, "application/json");
  res.sendChunk(F("["));
  char row[64];
  for (uint8_t i = 0; i < self->_scanCount; i++) {
    int n = snprintf(row, sizeof(row),
                     "%s{\"s\":\"%s\",\"r\":%d}",
                     i ? "," : "",
                     self->_scan[i].ssid, (int)self->_scan[i].rssi);
    if (n > 0) res.sendChunk(row, (size_t)n);
  }
  res.sendChunk(F("]"));
  res.endChunked();
}

void EGPortal::hSave(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);

  const char* ssid = req.arg("ssid");
  if (!ssid || !ssid[0]) {
    res.send(400, "text/plain", "ssid required");
    return;
  }
  // arg() returns a pointer into a shared static buffer; copy before next arg()
  strncpy(self->_savedSsid, ssid, EGPORTAL_SSID_MAX);
  self->_savedSsid[EGPORTAL_SSID_MAX] = '\0';

  const char* pass = req.arg("password");
  if (pass) {
    strncpy(self->_savedPass, pass, EGPORTAL_PASS_MAX);
    self->_savedPass[EGPORTAL_PASS_MAX] = '\0';
  } else {
    self->_savedPass[0] = '\0';
  }

  res.send(200, "text/html", FPSTR(SAVED_HTML));

  if (self->_onSave) self->_onSave(self->_savedSsid, self->_savedPass, self->_onSaveUser);
}

void EGPortal::hRescan(EGHttpRequest& req, EGHttpResponse& res, void* user) {
  EGPortal* self = static_cast<EGPortal*>(user);
  self->kickScan();
  res.send(200, "text/html", FPSTR(RESCAN_HTML));
}

void EGPortal::hCaptive(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Absolute redirect so the captive popup navigates onto our AP IP rather
  // than staying on the probe host (connectivitycheck.*, generate_204, etc).
  IPAddress ip = WiFi.softAPIP();
  char loc[40];
  snprintf(loc, sizeof(loc), "http://%u.%u.%u.%u/", ip[0], ip[1], ip[2], ip[3]);
  res.redirect(loc);
}
