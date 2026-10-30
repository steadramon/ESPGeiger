/*
  WebPortal.h - Async web UI

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
#ifndef WEBPORTAL_H
#define WEBPORTAL_H

#include <EGHttpServer.h>

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
// script and css urls, a new build fetches them again
#define EG_CACHE_BUST "?v=" GIT_VERSION

// a route table row (PROGMEM), smaller than an on() call; no user pointer
struct WebRoute { PGM_P path; EGHttpServer::Handler h; EGHttpRequest::Method method; };

class WebPortal {
  public:
    void begin(uint16_t port);
    static constexpr uint8_t TICK_MS = 13;
    void tick(uint32_t now);
    inline uint32_t next_tick_due() const { return _tick_last_ms + TICK_MS; }
    bool active() const { return _active; }

    // Shared page templates
    static void sendPageHead(EGHttpResponse& res, const __FlashStringHelper* title,
                             const char* inlineSub = nullptr);
    static void sendPageTail(EGHttpResponse& res, bool footer = false);
    static void sendPageFooter(EGHttpResponse& res);
    // a chunked html page: begin + head, tail + end
    static void beginPage(EGHttpResponse& res, const __FlashStringHelper* title,
                          const char* inlineSub = nullptr);
    static void endPage(EGHttpResponse& res, bool footer = false);
    static void on(EGHttpServer& http, const WebRoute* rows, size_t n);
    static void infoRow(EGHttpResponse& res, PGM_P key, PGM_P fmt, ...);   // key, fmt in flash
    static void metricsLine(EGHttpResponse& res, PGM_P fmt, ...);

    static void requestRestart(uint32_t delayMs = 1500);   // ok from async callbacks
    static void sendRestartCountdown(EGHttpResponse& res);
    static void sendNotSaved(EGHttpResponse& res);   // commit failed
    // a build's gzipped script or style, cached for good (gz in flash)
    static void sendGz(EGHttpResponse& res, const __FlashStringHelper* type,
                       const uint8_t* gz, size_t len);

    static void applyAuthFromPrefs();   // not in loop, walks pref tables

  private:
    EGHttpServer _http;
    bool _active = false;
    uint32_t _tick_last_ms = 0;

    // Non-module routes
    static void hRoot(EGHttpRequest&, EGHttpResponse&, void*);
    static void hStyleCss(EGHttpRequest&, EGHttpResponse&, void*);
    static void hRestart(EGHttpRequest&, EGHttpResponse&, void*);
    static void hGuard(EGHttpRequest&, EGHttpResponse&, void*);
    static void hReset(EGHttpRequest&, EGHttpResponse&, void*);
    static void hInfo(EGHttpRequest&, EGHttpResponse&, void*);
    static void hAbout(EGHttpRequest&, EGHttpResponse&, void*);
    static void hOutputs(EGHttpRequest&, EGHttpResponse&, void*);
    static void hMetrics(EGHttpRequest&, EGHttpResponse&, void*);
    static void hVars(EGHttpRequest&, EGHttpResponse&, void*);
    static void hFavicon(EGHttpRequest&, EGHttpResponse&, void*);
    static void hThemeJs(EGHttpRequest&, EGHttpResponse&, void*);
    static void hConsoleStream(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWebCmd(EGHttpRequest&, EGHttpResponse&, void*);
    static void hEraseWifi(EGHttpRequest&, EGHttpResponse&, void*);
    static void hParam(EGHttpRequest&, EGHttpResponse&, void*);
    static void hParamBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                           const char*, size_t, void*);
    static void hUpdateForm(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateDone(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateProgress(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                            const char*, size_t, void*);
    static void hWifi(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWifiScan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWifiSave(EGHttpRequest&, EGHttpResponse&, void*);
    static void hExport(EGHttpRequest&, EGHttpResponse&, void*);
    static void hImport(EGHttpRequest&, EGHttpResponse&, void*);
    static void hImportBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                            const char*, size_t, void*);
};

#endif
