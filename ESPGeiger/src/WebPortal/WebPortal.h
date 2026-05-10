/*
  WebPortal.h - Async web UI for ESPGeiger

  Owns one EGHttpServer instance, registers cross-cutting routes (/, /info,
  /style.css), and walks a static list of per-module registerRoutes() calls
  at boot. Replaces the WiFiManager + ESP8266WebServer running web UI.

  License: GPL-3.0
*/
#ifndef WEBPORTAL_H
#define WEBPORTAL_H

#include <EGHttpServer.h>

class WebPortal {
  public:
    void begin(uint16_t port);
    // Drives EGHttpServer dispatch + the restart timer. Throttled to
    // ~5 ms internally; passing `now` (millis) avoids a redundant time
    // read on the hot path. Caller already has it for loop_all etc.
    void tick(uint32_t now);
    bool active() const { return _active; }

    // Shared page templates - modules call these inside their handler so the
    // page chrome (HEAD, CSS link, top nav, h1, back button, footer) is
    // consistent across the whole UI. Caller owns the chunked accumulator.
    static void sendPageHead(EGHttpResponse& res, const __FlashStringHelper* title);
    static void sendPageTail(EGHttpResponse& res);

    // Credit + version → /about link. Bottom of / and /status. Kept
    // separate from sendPageTail because hRoot emits its own closing
    // </body></html> and hStatus inlines a STATUS_BODY chunk; calling
    // this before either keeps the visual footer consistent across both.
    static void sendPageFooter(EGHttpResponse& res);

    // Schedule ESP.restart() to fire after `delayMs`, giving in-flight async
    // responses time to drain. Safe to call from AsyncTCP callback context.
    static void requestRestart(uint32_t delayMs = 1500);

    // Emit a shared "restarting" countdown that auto-reloads / after ~20s.
    // Handlers emit their action-specific message first, then call this.
    // Auto-reload fails gracefully when the device moves networks
    // (reset/erase/wifisave); the countdown UX is the consistent part.
    static void sendRestartCountdown(EGHttpResponse& res);

    // Re-read sys.web_pass and reconfigure the underlying EGHttpServer's
    // Basic Auth. Called from SystemPrefs::on_prefs_loaded (boot) and
    // on_prefs_saved (after /param POST). Cheap when called rarely; do
    // NOT put this in the loop hot path - getString walks pref tables.
    static void applyAuthFromPrefs();

  private:
    EGHttpServer _http;
    bool _active = false;

    // Cross-cutting routes (owned here, not by any module).
    static void hRoot(EGHttpRequest&, EGHttpResponse&, void*);
    static void hStyleCss(EGHttpRequest&, EGHttpResponse&, void*);
    static void hRestart(EGHttpRequest&, EGHttpResponse&, void*);
    static void hReset(EGHttpRequest&, EGHttpResponse&, void*);
    static void hInfo(EGHttpRequest&, EGHttpResponse&, void*);
    static void hAbout(EGHttpRequest&, EGHttpResponse&, void*);
    static void hOutputs(EGHttpRequest&, EGHttpResponse&, void*);
    static void hFavicon(EGHttpRequest&, EGHttpResponse&, void*);
    static void hThemeJs(EGHttpRequest&, EGHttpResponse&, void*);
    static void hJs(EGHttpRequest&, EGHttpResponse&, void*);
    static void hStatus(EGHttpRequest&, EGHttpResponse&, void*);
    static void hConsoleStream(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWebCmd(EGHttpRequest&, EGHttpResponse&, void*);
    static void hEraseWifi(EGHttpRequest&, EGHttpResponse&, void*);
    static void hParam(EGHttpRequest&, EGHttpResponse&, void*);
    static void hParamBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                           const char*, size_t, void*);
    static void hUpdateForm(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateDone(EGHttpRequest&, EGHttpResponse&, void*);
    static void hUpdateBody(EGHttpRequest&, EGHttpServer::BodyEvent,
                            const char*, size_t, void*);
    static void hWifi(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWifiScan(EGHttpRequest&, EGHttpResponse&, void*);
    static void hWifiSave(EGHttpRequest&, EGHttpResponse&, void*);
};

#endif
