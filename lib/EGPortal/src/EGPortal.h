/*
  EGPortal.h - Captive WiFi setup portal on AsyncTCP.

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
#include <DNSServer.h>
#include "EGHttpServer.h"

#ifndef EGPORTAL_SCAN_MAX
  #define EGPORTAL_SCAN_MAX 16
#endif
#ifndef EGPORTAL_SSID_MAX
  #define EGPORTAL_SSID_MAX 32   // 802.11 SSID max
#endif
#ifndef EGPORTAL_PASS_MAX
  #define EGPORTAL_PASS_MAX 64   // WPA2 PSK ASCII max
#endif

class EGPortal {
  public:
    using SaveCallback = void(*)(const char* ssid, const char* password, void* user);
    using ResetCallback = void(*)(void* user);

    // Bring up AP, DNS catchall, and HTTP. AP password may be nullptr (open).
    void begin(const char* apSsid, const char* apPassword = nullptr);
    void loop();
    void end();

    // Fires on POST /save with valid creds. Pointers are owned by the portal
    // and remain valid only for the duration of the callback. Copy them.
    void onSave(SaveCallback cb, void* user = nullptr);

    // Optional factory-reset hook. When set, the setup page shows a
    // two-step confirm flow (GET /reset -> POST /reset_go); the callback
    // fires after the confirmation response is queued. Do NOT restart
    // inside the callback - set a flag and restart from the pump loop so
    // the HTTP response can flush first.
    void onReset(ResetCallback cb, void* user = nullptr);

    // Branding. Pointers must outlive the portal (PROGMEM / static).
    void setTitle(const char* t)  { _title  = t; }
    void setNotice(const char* n) { _notice = n; }

    bool active() const { return _active; }

    // ---- Shared scan helpers ----
    // ScanEntry is shared so WebPortal can reuse the dedup/sort path.
    struct ScanEntry {
      char    ssid[EGPORTAL_SSID_MAX + 1];
      int8_t  rssi;
      uint8_t enc;     // wl_enc_type / wifi_auth_mode_t per platform
    };

    // Process the result of WiFi.scanComplete() into a filtered/deduped/
    // sorted output buffer. rawCount = scanComplete() return. Returns the
    // populated entry count. Caller owns out[]; helper does no heap.
    static size_t processScan(ScanEntry* out, size_t maxOut, int rawCount,
                              int8_t rssiMin = -85);

  private:
    EGHttpServer _http;
    DNSServer    _dns;
    bool _active = false;

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
    uint32_t  _scanLast  = 0;

    static void hRoot(EGHttpRequest&, EGHttpResponse&, void*);
    static void hScan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hRescan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hSave(EGHttpRequest&, EGHttpResponse&, void*);
    static void hCaptive(EGHttpRequest&, EGHttpResponse&, void*);
    static void hResetConfirm(EGHttpRequest&, EGHttpResponse&, void*);
    static void hResetGo(EGHttpRequest&, EGHttpResponse&, void*);

    void refreshScan(int n);
    void kickScan();
};
