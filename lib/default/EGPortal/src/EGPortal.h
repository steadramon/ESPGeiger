/*
  EGPortal.h - Captive WiFi setup portal on AsyncTCP.

  Replaces WiFiManager-style flow: brings up a soft-AP, runs a DNS catchall
  so any hostname resolves to the device, serves a tiny form for SSID +
  password selection, fires onSave() when the user submits.

  Heap-friendly: zero String, fixed buffers, no per-request heap allocations.
  Generic: caller drives the WiFi connection attempt — portal only handles
  the setup-mode UI.

  License: GPL-3.0
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

    // Bring up AP, DNS catchall, and HTTP. AP password may be nullptr (open).
    void begin(const char* apSsid, const char* apPassword = nullptr);
    void loop();
    void end();

    // Fires on POST /save with valid creds. Pointers are owned by the portal
    // and remain valid only for the duration of the callback. Copy them.
    void onSave(SaveCallback cb, void* user = nullptr);

    // Branding. Pointers must outlive the portal (PROGMEM / static).
    void setTitle(const char* t)  { _title  = t; }
    void setNotice(const char* n) { _notice = n; }

    bool active() const { return _active; }

  private:
    EGHttpServer _http;
    DNSServer    _dns;
    bool _active = false;

    SaveCallback _onSave = nullptr;
    void* _onSaveUser = nullptr;

    const char* _title  = "Setup";
    const char* _notice = nullptr;

    char _savedSsid[EGPORTAL_SSID_MAX + 1] = {0};
    char _savedPass[EGPORTAL_PASS_MAX + 1] = {0};

    struct ScanEntry {
      char    ssid[EGPORTAL_SSID_MAX + 1];
      int8_t  rssi;
      uint8_t enc;
    };
    ScanEntry _scan[EGPORTAL_SCAN_MAX];
    uint8_t   _scanCount = 0;
    uint32_t  _scanLast  = 0;

    static void hRoot(EGHttpRequest&, EGHttpResponse&, void*);
    static void hScan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hRescan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hSave(EGHttpRequest&, EGHttpResponse&, void*);
    static void hCaptive(EGHttpRequest&, EGHttpResponse&, void*);

    void refreshScan(int n);
    void kickScan();
};
