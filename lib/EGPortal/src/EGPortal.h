/*
  EGPortal.h - Captive WiFi setup portal on EGTcp.

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
#pragma once

#include <Arduino.h>
#include "EGDnsAnswer.h"
#include "EGHttpServer.h"

#ifndef EGPORTAL_SCAN_MAX
  #define EGPORTAL_SCAN_MAX 16   // networks kept for the list
#endif
#ifndef EGPORTAL_SSID_MAX
  #define EGPORTAL_SSID_MAX 32   // 802.11 SSID max
#endif
#ifndef EGPORTAL_PASS_MAX
  #define EGPORTAL_PASS_MAX 64   // WPA2 PSK ASCII max
#endif
#define EGPORTAL_PASS_MIN 8   // WPA2
#ifndef EGPORTAL_RSSI_MIN
  #define EGPORTAL_RSSI_MIN -85   // dBm
#endif

class EGPortal {
  public:
    // false = not taken, the page says so
    using SaveCallback = bool(*)(const char* ssid, const char* password, void* user);
    using ResetCallback = void(*)(void* user);

    // a join test's state, the app keeps it
    enum Join : uint8_t {
      JOIN_IDLE, JOIN_TESTING, JOIN_BACK, JOIN_OK,
      JOIN_NO_SSID, JOIN_BAD_PASS, JOIN_REFUSED, JOIN_NO_REPLY,
      JOIN_NOT_SAVED,   // joined, the flash write failed
    };

    // blocks for one sync scan, then AP + dns + http on 80
    // false = the AP or dns didn't start, end() still owed
    __attribute__((warn_unused_result))
    bool begin(const char* apSsid, const char* apPassword = nullptr);   // nullptr = open
    void loop();   // every loop while active, handlers run from here
    void end();

    // pointers only valid in the callback
    void onSave(SaveCallback cb, void* user = nullptr);

    // Don't restart in here, set a flag
    void onReset(ResetCallback cb, void* user = nullptr);

    // every page, empty = open
    void setBasicAuth(const __FlashStringHelper* user, const char* password,
                      const __FlashStringHelper* realm = nullptr) {
      _http.setBasicAuth(user, password, realm);
    }
    void setAuthHelp(PGM_P text) { _http.setAuthHelp(text); }   // the 401's body

    // must outlive the portal
    void setTitle(const char* t)  { _title  = t; }
    void setNotice(const char* n) { _notice = n; }

    bool active() const { return _active; }
    bool busy() const;   // work waiting, loop() again soon

    void kickScan();   // async, loop() collects it
    bool heard(const char* ssid) const;   // in the last scan

    struct ScanEntry {
      char    ssid[EGPORTAL_SSID_MAX + 1];
      int8_t  rssi;    // dBm
      uint8_t enc;     // wl_enc_type / wifi_auth_mode_t per platform
      uint8_t chan;
    };

    // WiFi scan results into out: one per SSID, weak ones dropped, strongest first
    static size_t processScan(ScanEntry* out, size_t maxOut, int rawCount,
                              int8_t rssiMin = EGPORTAL_RSSI_MIN);

    // 1-32
    static bool ssidOk(const char* s) {
      return s && s[0] && strlen(s) <= EGPORTAL_SSID_MAX;
    }

    // "" = open, else 8-63, or 64 = the hex PSK. A missing field isn't open
    static bool passOk(const char* p) {
      if (!p) return false;
      size_t n = strlen(p);
      if (n == 0) return true;
      if (n < EGPORTAL_PASS_MIN || n > EGPORTAL_PASS_MAX) return false;
      if (n < EGPORTAL_PASS_MAX) return true;
      for (size_t i = 0; i < n; i++) {
        if (!isxdigit((unsigned char)p[i])) return false;
      }
      return true;
    }

    // a form's ssid and password, checked then copied out (arg() reuses its buffer)
    enum Creds : uint8_t { CREDS_OK, CREDS_SSID, CREDS_LONG, CREDS_PASS };
    static Creds takeCreds(EGHttpRequest& req, char (&ssid)[EGPORTAL_SSID_MAX + 1],
                           char (&pass)[EGPORTAL_PASS_MAX + 1]) {
      const char* s = req.arg(F("ssid"));
      // a cut one would fail to join
      if (!ssidOk(s)) return CREDS_SSID;
      strlcpy(ssid, s, sizeof(ssid));
      const char* p = req.arg(F("password"));
      if (p && strlen(p) > EGPORTAL_PASS_MAX) return CREDS_LONG;
      if (!passOk(p)) return CREDS_PASS;
      strlcpy(pass, p, sizeof(pass));
      return CREDS_OK;
    }
    static PGM_P credsText(Creds c);   // not CREDS_OK
    static const char NOT_SAVED[];     // PROGMEM, out of memory

  private:
    EGHttpServer _http;
    EGDnsAnswer  _dns;
    bool _active = false;
    bool _routed = false;

    SaveCallback _onSave = nullptr;
    void* _onSaveUser = nullptr;

    ResetCallback _onReset = nullptr;
    void* _onResetUser = nullptr;

    const char* _title  = "Setup";
    const char* _notice = nullptr;

    ScanEntry _scan[EGPORTAL_SCAN_MAX];
    uint8_t   _scanCount = 0;

    static void hRoot(EGHttpRequest&, EGHttpResponse&, void*);
    static void hScan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hRescan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hSave(EGHttpRequest&, EGHttpResponse&, void*);
    static void hCaptive(EGHttpRequest&, EGHttpResponse&, void*);
    static void hResetConfirm(EGHttpRequest&, EGHttpResponse&, void*);
    static void hResetGo(EGHttpRequest&, EGHttpResponse&, void*);

    void refreshScan(int n);
    void takeScan(int n);
};
