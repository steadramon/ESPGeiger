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
#ifndef CORIUM_WEBPORTAL_H
#define CORIUM_WEBPORTAL_H

#include <EGHttpServer.h>

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
// script and css urls, a new build fetches them again
#define CORIUM_CACHE_BUST "?v=" GIT_VERSION
#ifndef CORIUM_HTTP_POLL_MS
#define CORIUM_HTTP_POLL_MS 13   // ms between idle http ticks
#endif

namespace corium {
// a route table row (PROGMEM), smaller than an on() call; no user pointer
struct WebRoute { PGM_P path; EGHttpServer::Handler h; EGHttpRequest::Method method; };

class WebPortal {
  public:
    void begin(uint16_t port);
    static constexpr uint8_t POLL_MS = CORIUM_HTTP_POLL_MS;   // idle, work waiting goes next ms
    // loop: http tick, then a due restart
    void poll(uint32_t now);
    inline uint32_t nextPollDue() const { return _poll_last_ms + pollGap(); }
    bool active() const { return _active; }

    // Shared page templates
    // inlineSub: small tag after the title, escaped here
    static void sendPageHead(EGHttpResponse& res, const __FlashStringHelper* title,
                             const char* inlineSub = nullptr);
    static void sendPageTail(EGHttpResponse& res, bool footer = false);
    static void sendPageFooter(EGHttpResponse& res);
    // a chunked html page: begin + head, tail + end
    static void beginPage(EGHttpResponse& res, const __FlashStringHelper* title,
                          const char* inlineSub = nullptr);
    static void endPage(EGHttpResponse& res, bool footer = false);
    static void on(EGHttpServer& http, const WebRoute* rows, size_t n);   // adds a PROGMEM table
    static void infoRow(EGHttpResponse& res, PGM_P key, PGM_P fmt, ...);   // key, fmt in flash
    static void sendf(EGHttpResponse& res, PGM_P fmt, ...);   // one chunk, a cut line is dropped

    static constexpr uint32_t RESTART_MS = 1500;
    static void requestRestart(uint32_t delay_ms = RESTART_MS);   // ok from async callbacks
    // the countdown, endPage, then the restart
    static void endRestartPage(EGHttpResponse& res, uint32_t delay_ms = RESTART_MS);
    static void sendNotSaved(EGHttpResponse& res);   // commit failed
    // a build's gzipped script or style, cached for good (gz in flash)
    static void sendGz(EGHttpResponse& res, const __FlashStringHelper* type,
                       const uint8_t* gz, size_t len);

    static void applyAuthFromPrefs();   // not in loop, walks pref tables
    static void setHomeName(PGM_P name);   // the root page's name, THING_NAME till set

  private:
    EGHttpServer _http;
    bool _active = false;
    uint32_t _poll_last_ms = 0;
    // a request waiting goes next ms
    inline uint8_t pollGap() const { return _active && _http.tickWanted() ? 1 : POLL_MS; }

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
    static void hParamJs(EGHttpRequest&, EGHttpResponse&, void*);
    static bool hParamBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                           const char*, size_t, void*);
    static void hUpdateForm(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateJs(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateDone(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateProgress(EGHttpRequest&, EGHttpResponse&, void*);
    static bool hUpdateBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                            const char*, size_t, void*);
    static void hWifi(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWifiScan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWifiSave(EGHttpRequest&, EGHttpResponse&, void*);
    static void hExport(EGHttpRequest&, EGHttpResponse&, void*);
    static void hImport(EGHttpRequest&, EGHttpResponse&, void*);
    static bool hImportBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                            const char*, size_t, void*);
};
}  // namespace corium

#endif
