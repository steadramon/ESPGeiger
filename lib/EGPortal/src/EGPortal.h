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
    using SaveCallback = void(*)(const char* ssid, const char* password, void* user);
    using ResetCallback = void(*)(void* user);

    // blocks for one sync scan, then AP + dns + http on 80
    void begin(const char* apSsid, const char* apPassword = nullptr);   // nullptr = open
    void loop();   // every loop while active, handlers run from here
    void end();

    // pointers only valid in the callback
    void onSave(SaveCallback cb, void* user = nullptr);

    // dont restart in here, set a flag
    void onReset(ResetCallback cb, void* user = nullptr);

    // every page, empty = open
    void setBasicAuth(const __FlashStringHelper* user, const char* password,
                      const __FlashStringHelper* realm = nullptr) {
      _http.setBasicAuth(user, password, realm);
    }

    // must outlive the portal
    void setTitle(const char* t)  { _title  = t; }
    void setNotice(const char* n) { _notice = n; }

    bool active() const { return _active; }

    struct ScanEntry {
      char    ssid[EGPORTAL_SSID_MAX + 1];
      int8_t  rssi;    // dBm
      uint8_t enc;     // wl_enc_type / wifi_auth_mode_t per platform
    };

    // WiFi scan results into out: one per SSID, weak ones dropped, strongest first
    static size_t processScan(ScanEntry* out, size_t maxOut, int rawCount,
                              int8_t rssiMin = EGPORTAL_RSSI_MIN);

    // 1-32
    static bool ssidOk(const char* s) {
      return s && s[0] && strlen(s) <= EGPORTAL_SSID_MAX;
    }

    // "" = open, else 8-63, or 64 = the hex PSK; no field isn't open
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

    char _savedSsid[EGPORTAL_SSID_MAX + 1] = {0};
    char _savedPass[EGPORTAL_PASS_MAX + 1] = {0};

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
    void kickScan();
    void takeScan(int n);
};
