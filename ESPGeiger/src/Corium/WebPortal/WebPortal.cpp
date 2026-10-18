/*
  WebPortal.cpp - Async web UI for ESPGeiger

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
#include "WebPortal.h"
#include "ImportText.h"
#include "Corium/Module/EGModule.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Prefs/SystemPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/CrashDump.h"
#include "Corium/System/BootGuard.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/System/Lifecycle.h"
#include <EGEscape.h>
#include <EGJson.h>
#include <EGPortal.h>
#include "Corium/Logger/Logger.h"
#include "Corium/NTP/NTP.h"
#include "Corium/System/Clock.h"
#include "Corium/System/AppHooks.h"
#include <LittleFS.h>
#include <EGBase64.h>

#ifdef ESP8266
  #include <ESP8266WiFi.h>
  #include <user_interface.h>
#else
  #include <WiFi.h>
  #include <Update.h>   // esp32 needs it
#endif

#include "STYLE_CSS.gz.h"
#include "THEME_JS.gz.h"
#include "JS_BUNDLE.gz.h"
#include "HVJS_BUNDLE.gz.h"

extern const char picographJS[] PROGMEM;

#define CACHE_1D "public, max-age=86400"
#define CACHE_7D "public, max-age=604800"
extern const char wifiAdvancedHTML[] PROGMEM;

// CSS + page templates

#if !EG_GZ_STYLE_CSS
static const char STYLE_CSS[] PROGMEM = R"CSS(:root{--bg:#fff;--page:#f8f9fa;--fg:#212529;--muted:#6c757d;--border:#dee2e6;--accent:#06c;--link:#06c;--card:#f8f9fa}
:root[data-theme=dark]{--bg:#212529;--page:#1a1d20;--fg:#dee2e6;--muted:#adb5bd;--border:#373b3e;--card:#2b3035;--link:#6aa9f0}
*{box-sizing:border-box}
html{background:var(--page);min-height:100%}
body{font:16px/1.5 -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;max-width:780px;margin:1.5em auto;padding:1.5em 1em;color:var(--fg);background:var(--bg);border-radius:8px;box-shadow:0 1px 3px #0000001a}
@media(max-width:640px){body{margin:0 auto;border-radius:0;box-shadow:none}}
h1{font-size:1.5em;margin:0 0 1em;font-weight:500}
h2{font-size:.9em;margin:.6em 0 .3em;font-weight:700;color:var(--fg);text-transform:uppercase;letter-spacing:.05em}
a{color:var(--link);text-decoration:none}a:hover{text-decoration:underline}
input,select,textarea,button{font:inherit;padding:.5em .7em;border:1px solid var(--border);border-radius:4px;background:var(--bg);color:var(--fg);width:100%}
input[type=radio],input[type=checkbox]{width:auto;margin:0 .4em 0 0;vertical-align:middle}
input:focus,select:focus,textarea:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 2px #0066cc26}
button,input[type=submit]{background:var(--accent);color:#fff;border:0;cursor:pointer;width:auto;padding:.5em 1em}
form>button[type=submit]{margin-top:.8em}
button.danger{background:#c0392b}button.danger:hover{background:#a02a1f}.btn-o{background:transparent;color:var(--link);border:1px solid var(--link)}.btn-o:hover{background:var(--accent);color:#fff}.r{display:block;margin:.5em 0 0 auto}
details{margin:.6em 0}
fieldset{border:1px solid var(--border);border-radius:4px;padding:.8em 1em;margin:1em 0}
legend{padding:0 .3em;color:var(--muted)}
label{display:block;margin:.5em 0 .2em}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:.4em .6em;border-bottom:1px solid var(--border)}
th{font-weight:500;color:var(--muted)}
.muted{color:var(--muted);font-size:.9em}
.stat{display:grid;grid-template-columns:auto 1fr auto 1fr;column-gap:2em}
.stat>.row{display:grid;grid-template-columns:subgrid;grid-column:1/-1;padding:.4em .6em;border-bottom:1px solid var(--border);align-items:baseline}
.stat>.row b{color:var(--muted);font-weight:500;font-family:inherit}
.stat>.row.envflex{display:flex;gap:2em}
.stat>.row.envflex>div{flex:1;display:flex;gap:2em;align-items:baseline}
.card{background:var(--card);border:1px solid var(--border);border-radius:4px;padding:1em;margin:1em 0}
.bar-row{display:flex;align-items:flex-end;gap:1px;height:80px;width:100%;overflow:hidden;margin:.4em 0;border-bottom:1px solid var(--border)}
.bar-row.bot{margin:.4em 0 1em}
.bar-lbls{display:flex;gap:1px;font-size:.65em;color:var(--muted);white-space:nowrap;overflow:hidden}
.bar-cell{flex:1;min-width:0}
.a{padding:.5em .7em;margin:.6em 0;border-radius:4px;border-left:4px solid;font-size:.9em}
.ak{background:#1f6f3a26;border-color:#1f6f3a}
.aw{background:#ffc40026;border-color:#ffc40099}
.ae{background:#c0392b26;border-color:#c0392b}
.cl{float:right;cursor:pointer;font-size:1.3em;line-height:1;opacity:.6}.cl:hover{opacity:1}
.il{display:inline-block;margin:.4em 1em .4em 0}.il input{width:auto;margin-right:.3em;vertical-align:middle}
small{display:block;color:var(--muted);font-size:.85em;margin:.1em 0 .4em}
hr{border:0;border-top:1px solid var(--border);margin:1em 0}
h4{margin:.8em 0 .2em;font-size:1em;color:var(--muted)}
output{display:inline-block;margin-left:.6em;color:var(--muted)}
details.bx{border:1px solid var(--border);border-radius:4px;padding:.6em .8em;margin:.6em 0}
details.bx>summary{font-weight:500;cursor:pointer;color:var(--muted)}
details.adv{margin:.5em 0 0;border:0;padding:0}details.adv>summary{color:var(--muted);font-size:.85em;font-weight:400}
details.bx.off>summary{opacity:.55}
.gb{font-size:.68em;font-weight:400;padding:.05em .45em;border-radius:3px;margin-left:.5em;border:1px solid var(--border);color:var(--muted)}.gb.on{color:var(--link);border-color:var(--link)}
nav.tabs{display:flex;border-bottom:1px solid var(--border);margin:0 0 1em;gap:.4em;flex-wrap:wrap}
nav.tabs a{padding:.5em .9em;color:var(--muted);border-bottom:2px solid transparent;text-decoration:none}
nav.tabs a:hover{color:var(--fg)}
nav.tabs a.ac{color:var(--link);border-bottom-color:var(--link)}
.fx{display:flex;gap:.5em}.mt{margin-top:.4em}.mn{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
#dyB{position:fixed;top:0;left:0;right:0;padding:.5em;background:#fc3;color:#000;text-align:center;z-index:9999;cursor:pointer;font-size:.9em;font-weight:600}
.menu{display:flex;flex-direction:column;gap:.85em;margin:1em auto;max-width:22em}
.menu a,.back{background:var(--accent);color:#fff;font-weight:500;padding:.5em 1em;border-radius:4px}
.menu a{display:block;text-align:center;font-size:1.15em;padding:.55em 1em;border-radius:6px}
.menu.dense{gap:.35em}.menu.dense a{font-size:.95em;padding:.3em .8em}
.back{display:inline-block}
.menu a:hover,.back:hover{opacity:.9;text-decoration:none}
.back-row{margin-top:2em}
.btn-sm{font-size:.85em;padding:.2em .6em;width:auto}
.themetoggle{position:fixed;top:.8em;right:.8em;width:2.4em;height:2.4em;border-radius:50%;border:1px solid var(--border);background:var(--card);color:var(--fg);cursor:pointer;font-size:1.1em;line-height:1;padding:0;display:flex;align-items:center;justify-content:center;z-index:10}
.themetoggle::before{content:'\2600'}
:root[data-theme=dark] .themetoggle::before{content:'\263E'}
.devhead{margin:0 0 1em}
.devhead h1{margin:0;font-size:1.65em;font-weight:700;display:flex;align-items:center;gap:.5em}
.devhead h1 img{width:1.4em;height:1.4em;flex:0 0 auto}
.devhead h1 .tag{font-size:.6em;font-weight:500;color:var(--muted);margin-left:.4em;opacity:.85}
.devhead .sub{color:var(--muted);font-size:.95em;margin-top:.2em}
.cred{margin-top:1.5em;font-size:.85em;color:var(--muted)}
.cred a{color:inherit}
.cred .brand{color:var(--fg);font-weight:bold;font-family:"Courier New",Courier,monospace}
#g1{height:200px;width:100%;background:var(--bg);border:1px solid var(--border);border-radius:4px}
#g2{display:flex;flex-wrap:wrap;gap:.2em 1.2em;margin:.4em 0;font-size:1.05em}
#t1{width:100%;height:250px;font-family:monospace;font-size:.8em;line-height:1.3;resize:vertical}
:root.crt{--bg:#000;--page:#000;--fg:#0f0;--muted:#080;--border:#050;--accent:#0a0;--link:#0a0;--card:#001a00}
:root.crt[data-theme=light]{--bg:#212529;--card:#2b3035;--border:#373b3e;background:#1a1d20}
:root.crt[data-theme=dark]{background:#000}
:root.crt body{font-family:Consolas,Menlo,monospace;font-weight:600;box-shadow:none}
:root.crt :is(h1,h2,h3){color:#0f0;text-shadow:0 0 4px #0f0;font-weight:700}
:root.crt :is(th,td,p,span,div){font-weight:600}
:root.crt :is(button:not(.btn-o),input[type=submit],.menu a,.back){color:#000;text-shadow:none}
:root.crt .devhead h1 .tag{color:#0a0;text-shadow:none;opacity:1}
:root.crt::after{content:'';position:fixed;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.18) 0,rgba(0,0,0,.18) 1px,transparent 1px,transparent 3px);z-index:9998;animation:crtroll 4s linear infinite}
@keyframes crtroll{to{background-position:0 3px}})CSS";
#endif

// Trefoil, one flash copy
static const char FAVICON_PATHS[] PROGMEM =
  "<circle cx='256' cy='256' r='256' fill='#666'/>"
  "<circle cx='256' cy='256' r='220' fill='#FB2'/>"
  "<path fill='#555' d='M256 286a30 30 0 1 0 0-60 30 30 0 0 0 0 60zm28-82 62-109a182 182 0 0 0-182 1l63 109a57 57 0 0 1 57-1zm155 51H313c0 21-11 39-28 49l64 108c54-32 90-90 90-157zM163 412l64-108a57 57 0 0 1-28-49H73c0 67 36 125 90 157z'/>";

static const char BACK_ROW[] PROGMEM =
  "<p class=back-row><a class=back href=/>\xe2\x86\x90 Home</a></p>";
static const char HTML_END[] PROGMEM = "</body></html>";

static const char HEAD_OPEN[] PROGMEM =
  "<!DOCTYPE html><html lang=en><head>"
  "<meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<link rel=icon type=image/svg+xml href=/favicon.svg>";
static const char HEAD_STYLE[] PROGMEM =
  "<link rel=stylesheet href=/style.css" EG_CACHE_BUST "><title>";
static const char SAFE_STYLE[] PROGMEM =
  "<style>html{background:#161616;border-top:12px solid;"
  "border-image:repeating-linear-gradient(45deg,#fb0 0 10px,#161616 0 20px) 12}"
  "body{color:#ddd;font-family:monospace;max-width:420px;margin:0;padding:0 1em}"
  "h1{font-size:1.3em}h1 svg{vertical-align:middle;margin-right:.4em}h1 b,a{color:#fb0}.menu a{display:block;margin:.8em 0}"
  "pre{white-space:pre-wrap}label,small{display:block}label{margin-top:.6em}small{color:#999}"
  ".il{display:inline-block;margin-right:1em}nav a{margin-right:1em}</style><title>";

static const char HEAD_THEME[] PROGMEM =
  "</title><script src=/theme.js" EG_CACHE_BUST "></script></head><body>"
  "<button class=themetoggle aria-label=\"Toggle theme\" onclick=theme()></button>";
static const char SAFE_THEME[] PROGMEM =
  " (safe mode)</title><script>var byID=t=>document.getElementById(t)</script></head><body>";
static const char HEAD_H1[] PROGMEM =
  "<div class=devhead>"
  "<h1><svg viewBox='0 0 512 512' style='width:1.4em;height:1.4em;flex:0 0 auto'>";

static const char SVG_END[] PROGMEM = "</svg>";

static void sendHeadOpen(EGHttpResponse& res) {
  res.sendChunk(FPSTR(HEAD_OPEN));
  res.sendChunk(FPSTR(BootGuard::safe_mode() ? SAFE_STYLE : HEAD_STYLE));
}

static void sendHeadTail(EGHttpResponse& res) {
  res.sendChunk(FPSTR(BootGuard::safe_mode() ? SAFE_THEME : HEAD_THEME));
  res.sendChunk(FPSTR(HEAD_H1));
  res.sendChunk(FPSTR(FAVICON_PATHS));
  res.sendChunk(FPSTR(SVG_END));
}

static void sendSafeTag(EGHttpResponse& res) {
  if (BootGuard::safe_mode()) res.sendChunk(F(" <b>SAFE MODE</b>"));
}

static void sendSafeBanner(EGHttpResponse& res) {
  if (!BootGuard::safe_mode()) return;
  uint32_t up = fast_millis();
  uint32_t at = BootGuard::retry_after();
  char b[160];
  int n = snprintf_P(b, sizeof(b),
      PSTR("<p>Normal mode again in %u min, or <a href=/restart>restart now</a>.</p>"),
      (unsigned)(up < at ? (at - up) / 60000 + 1 : 0));
  if (n > 0 && (size_t)n < sizeof(b)) res.sendChunk(b, (size_t)n);
}

// after the app's own
static const EGMenuEntry WEBPORTAL_TOP_MENU[] PROGMEM = {
  {"/param",   "Config"},
  {"/network", "Network Config"},
  {"", ""}
};
static const EGMenuEntry WEBPORTAL_BOTTOM_MENU[] PROGMEM = {
  {"/info",    "Info"},
  {"/update",  "Update"},
  {"/restart", "Reboot"},
  {"", ""}
};

static void renderMenu(EGHttpResponse& res, const EGMenuEntry* e) {
  for (; e; e++) {
    EGMenuEntry m;
    memcpy_P(&m, e, sizeof(m));
    if (!m.path[0]) break;
    char btn[160];
    int n = snprintf_P(btn, sizeof(btn),
      PSTR("<a href='%s'>%s</a>"), m.path, m.label);
    size_t len = (n > 0) ? (size_t)n : 0;
    if (len >= sizeof(btn)) len = sizeof(btn) - 1;
    if (len) res.sendChunk(btn, len);
  }
}

// Pending restart

void WebPortal::requestRestart(uint32_t delayMs) {
  Lifecycle::request_restart(fast_millis(), delayMs);
}

// tick() applies these, page drains first
static volatile bool s_pendingWifiSave = false;   // creds in Wifi::holdCreds

static volatile bool s_pendingEraseCreds = false;
static volatile bool s_pendingFactoryReset = false;

static WebPortal* s_activeWebPortal = nullptr;

void WebPortal::tick(uint32_t now) {
  if (now - _tick_last_ms < 13) return;
  _tick_last_ms = now;

  if (_active) _http.tick();

  if (Lifecycle::restart_due(now)) {
    if (s_pendingWifiSave) {
      Log::console(PSTR("WebPortal: applying new WiFi creds (ssid='%s')"),
                   Wifi::heldSsid());
      Wifi::beginHeld();
      s_pendingWifiSave = false;
      delay(150);  // NOLINT(loop-block) restart follows, NVS settle
    }
    if (s_pendingEraseCreds) {
      Wifi::clearSavedCreds();
      s_pendingEraseCreds = false;
      delay(150);  // NOLINT(loop-block) restart follows
    }
    if (s_pendingFactoryReset) {
      DeviceInfo::factoryReset();
      s_pendingFactoryReset = false;
      delay(150);  // NOLINT(loop-block) restart follows
    }
    Log::console(PSTR("WebPortal: restarting"));
    DeviceInfo::safeRestart(50);
  }
}


static const char P_PARAM[]   PROGMEM = "/param";
static const char P_UPDATE[]  PROGMEM = "/update";
static const char P_IMPORT[]  PROGMEM = "/import";
static const char P_RESTART[] PROGMEM = "/restart";

#ifndef SAFE_HTTP_CLIENTS
#define SAFE_HTTP_CLIENTS 3   // page, favicon, one spare
#endif

void WebPortal::begin(uint16_t port) {
  if (_active) return;
  _http.on(F("/"),            EGHttpRequest::GET, &WebPortal::hRoot,        this);
  _http.on(F("/style.css"),   EGHttpRequest::GET, &WebPortal::hStyleCss,    nullptr);
  _http.on(F("/favicon.svg"), EGHttpRequest::GET, &WebPortal::hFavicon,     nullptr);
  _http.on(F("/favicon.ico"), EGHttpRequest::GET, &WebPortal::hFavicon,     nullptr);
  _http.on(F("/theme.js"),    EGHttpRequest::GET, &WebPortal::hThemeJs,     nullptr);
  _http.on(FPSTR(P_RESTART), EGHttpRequest::GET, &WebPortal::hRestart,       nullptr);
  _http.on(F("/guard"),     EGHttpRequest::GET, &WebPortal::hGuard,         nullptr);
  _http.on(F("/reset"),     EGHttpRequest::POST, &WebPortal::hReset,        nullptr);
  _http.on(F("/erase"),     EGHttpRequest::POST, &WebPortal::hEraseWifi,    nullptr);
  _http.on(F("/info"),      EGHttpRequest::GET, &WebPortal::hInfo,          nullptr);
  _http.on(F("/about"),     EGHttpRequest::GET, &WebPortal::hAbout,         nullptr);
  bool safe = BootGuard::safe_mode();
  if (safe) _http.setMaxClients(SAFE_HTTP_CLIENTS);
  _http.on(F("/cs"),        EGHttpRequest::GET, &WebPortal::hConsoleStream, nullptr);
  if (!safe) _http.on(F("/webcmd"), EGHttpRequest::POST, &WebPortal::hWebCmd, nullptr);
  _http.on(F("/outputs"),   EGHttpRequest::GET, &WebPortal::hOutputs,       nullptr);
  _http.on(F("/metrics"),   EGHttpRequest::GET, &WebPortal::hMetrics,       nullptr);
  _http.on(F("/vars.json"), EGHttpRequest::GET, &WebPortal::hVars,          nullptr);
  _http.on(FPSTR(P_PARAM),  EGHttpRequest::GET, &WebPortal::hParam,         nullptr);
  _http.on(FPSTR(P_PARAM),  EGHttpRequest::POST, &WebPortal::hParam,        nullptr);
  _http.onUpload(FPSTR(P_PARAM), &WebPortal::hParamBody, nullptr);
  _http.on(FPSTR(P_UPDATE), EGHttpRequest::GET,  &WebPortal::hUpdateForm,   nullptr);
  _http.on(FPSTR(P_UPDATE), EGHttpRequest::POST, &WebPortal::hUpdateDone,   nullptr);
  _http.onUpload(FPSTR(P_UPDATE), &WebPortal::hUpdateBody, nullptr);
  _http.on(F("/update/progress"), EGHttpRequest::GET, &WebPortal::hUpdateProgress, nullptr);
  _http.on(F("/network"),   EGHttpRequest::GET,  &WebPortal::hWifi,         nullptr);
  _http.on(F("/wifiscan"),  EGHttpRequest::GET,  &WebPortal::hWifiScan,     nullptr);
  _http.on(F("/wifisave"),  EGHttpRequest::POST, &WebPortal::hWifiSave,     nullptr);
  _http.on(F("/export"),    EGHttpRequest::GET,  &WebPortal::hExport,       nullptr);
  _http.on(FPSTR(P_IMPORT), EGHttpRequest::POST, &WebPortal::hImport,       nullptr);
  _http.onUpload(FPSTR(P_IMPORT), &WebPortal::hImportBody, nullptr);

  if (!safe) AppHooks::routes(_http);

  // Module routes
  uint8_t n = EGModuleRegistry::count();
  for (uint8_t i = 0; i < n; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (m && !EGModuleRegistry::quarantined(m)) m->registerRoutes(_http);
  }

  _http.begin(port);
  _active = true;
  s_activeWebPortal = this;
  applyAuthFromPrefs();
}

void WebPortal::applyAuthFromPrefs() {
  WebPortal* p = s_activeWebPortal;
  if (!p || !p->_active) return;
  const char* cur = EGPrefs::getString("sys", EG_K_WEB_PASS);
  static_assert(sizeof("admin") - 1 + 1 + WEB_PASS_MAX <= EGHTTP_AUTH_MAX,
                "admin:pass must fit EGHTTP_AUTH_MAX or auth switches off");
  p->_http.setBasicAuth(F("admin"), cur);
  static int8_t s_last_state = -1;
  int8_t state = (cur && *cur) ? 1 : 0;
  if (state != s_last_state) {
    Log::console(PSTR("WebPortal: auth %s"),
                 state ? PSTR("enabled (user=admin)") : PSTR("disabled"));
    s_last_state = state;
  }
}

void WebPortal::sendPageHead(EGHttpResponse& res, const __FlashStringHelper* title,
                             const char* inlineSub) {
  const char* fname = DeviceInfo::friendlyName();
  const char* devName = (fname && fname[0]) ? fname : DeviceInfo::hostname();
  char esc[6 * EG_NAME_MAX + 1];
  egesc_html(esc, sizeof(esc), devName);
  sendHeadOpen(res);
  res.sendChunk(title);
  res.sendKV(F(" \xc2\xb7 "), esc);
  sendHeadTail(res);
  res.sendChunk(F(THING_NAME " - "));
  res.sendChunk(title);
  if (inlineSub && inlineSub[0]) {
    egesc_html(esc, sizeof(esc), inlineSub);
    res.sendKV(F("<span class=tag>"), esc, F("</span>"));
  }
  sendSafeTag(res);
  res.sendChunk(F("</h1></div>"));
  sendSafeBanner(res);
}

void WebPortal::sendPageTail(EGHttpResponse& res, bool footer) {
  res.sendChunk(FPSTR(BACK_ROW));
  if (footer) WebPortal::sendPageFooter(res);
  res.sendChunk(FPSTR(HTML_END));
}

void WebPortal::sendPageFooter(EGHttpResponse& res) {
  res.sendChunk(F("<p class=cred>"
                  "<a class=brand href='" APP_URL "' target=_blank rel=noopener>" THING_NAME "</a>"
                  " &middot; <a href='/about'>" RELEASE_VERSION "</a></p>"));
}

static void root_safe_note(EGHttpResponse& res) {
  res.sendChunk(BootGuard::crash_streak() ? F("<pre>" THING_NAME " keeps crashing.\n")
                                          : F("<pre>Safe mode was requested.\n"));
  res.sendChunk(F("Undo a recent change in Config or install a previous firmware via Update.\n"
                  "Info shows the latest crash for bug reports.</pre>"));
}

static void root_menus(EGHttpResponse& res, bool safe) {
  res.sendChunk(F("<div class=menu>"));
  if (!safe) renderMenu(res, AppHooks::menu());
  renderMenu(res, WEBPORTAL_TOP_MENU);
  uint8_t mc = EGModuleRegistry::count();
  for (uint8_t i = 0; i < mc; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (m && !EGModuleRegistry::quarantined(m)) renderMenu(res, m->menuEntries());
  }
  renderMenu(res, WEBPORTAL_BOTTOM_MENU);
  res.sendChunk(F("</div>"));
}

void WebPortal::hRoot(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));

  const char* fname = DeviceInfo::friendlyName();
  bool hasFriendly = (fname && fname[0]);
  const char* hostname = DeviceInfo::hostname();
  char ipBuf[EG_IPV4_MAX + 1];
  Wifi::formatIP(ipBuf, sizeof(ipBuf), WiFi.localIP());

  char esc[6 * EG_NAME_MAX + 1];
  egesc_html(esc, sizeof(esc), fname);

  auto emitIdentity = [&]() {
    if (hasFriendly) res.sendKV(F(APP_HOME_NAME " - "), esc);
    else             res.sendChunk(F(APP_HOME_NAME));
  };

  bool safe = BootGuard::safe_mode();
  sendHeadOpen(res);
  emitIdentity();
  sendHeadTail(res);
  emitIdentity();
  sendSafeTag(res);
  res.sendKV(F("</h1><div class=sub>"), hostname);
  res.sendKV(F(" &middot; "), ipBuf, F("</div></div>"));
  sendSafeBanner(res);
  if (safe) root_safe_note(res);
  root_menus(res, safe);
  if (!safe) WebPortal::sendPageFooter(res);
  res.sendChunk(FPSTR(HTML_END));
  res.endChunked();
}

void WebPortal::hStyleCss(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if EG_GZ_STYLE_CSS
  res.sendGzipP(200, F("text/css"), STYLE_CSS_GZ, STYLE_CSS_GZ_LEN);
#else
  res.send(200, F("text/css"), FPSTR(STYLE_CSS));
#endif
}

void WebPortal::hFavicon(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), F(CACHE_7D));
  res.beginChunked(200, F("image/svg+xml"));
  res.sendChunk(F("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>"));
  res.sendChunk(FPSTR(FAVICON_PATHS));
  res.sendChunk(FPSTR(SVG_END));
  res.endChunked();
}

#if !EG_GZ_THEME_JS
static const char THEME_JS[] PROGMEM = R"JS(var byID=t=>document.getElementById(t);
!function(){
var d=document.documentElement,L=addEventListener,LS=localStorage,SS=sessionStorage,
TE=()=>dispatchEvent(new Event('themechange')),
AR=el=>{
  var u=d.classList.contains('crt'),
      list=el&&el.dataset?[el]:document.querySelectorAll('.usv');
  list.forEach(e=>{
    var v=parseFloat(e.dataset.uv);
    if(isNaN(v))return;
    e.textContent=u?(v*114).toFixed(v*114>=10?1:2):v.toFixed(3);
    e.title=u?v.toFixed(3)+' µSv/h':'';
  });
  document.querySelectorAll('.usvL').forEach(e=>{e.textContent=u?(e.dataset.on||'µR/h'):(e.dataset.off||'µSv/h')});
},
AD=el=>{
  var u=d.classList.contains('crt'),
      list=el&&el.dataset?[el]:document.querySelectorAll('.dose');
  list.forEach(e=>{
    var v=parseFloat(e.dataset.dose);
    if(isNaN(v))return;
    var dv=u?v*114:v,s=u?'R':'Sv',p='µ',sc=1;
    if(dv>=1e6){p='';sc=1e6}
    else if(dv>=1e3){p='m';sc=1e3}
    dv/=sc;
    e.textContent=(dv>=10?dv.toFixed(1):dv.toFixed(2))+' '+p+s;
  });
};
window.setUsv=(el,v)=>{el.dataset.uv=v;AR(el)};
window.setDose=(el,v)=>{el.dataset.dose=v;AD(el)};
window.applyRad=AR;
window.toggleCrt=()=>{LS.crt=d.classList.toggle('crt')?'1':'0';AR();AD();TE();window.applySnd&&window.applySnd()};
function C(){var s=LS.crt,ds=new Date().toDateString().substr(4,6),hh=2166136261;
for(var hi=0;hi<6;hi++)hh=Math.imul(hh^ds.charCodeAt(hi),16777619)>>>0;
if(s==='1')d.classList.add('crt');
else if(s==='0')d.classList.remove('crt');
AR();AD();
var msg={1332614041:'4piiIEhhcHB5IGJpcnRoZGF5IE1hcmllIEN1cmllIC0gYm9ybiAxODY3Lg==',1368767674:'8J+puyBIYXBweSBiaXJ0aGRheSBXaWxoZWxtIFLDtm50Z2VuIC0gYm9ybiAxODQ1Lg=='}[hh];
if(msg&&location.pathname==='/'&&SS.dayDis!=hh&&!byID('dyB')){var p=document.createElement('div');p.id='dyB';p.textContent=atob(msg);p.onclick=()=>{SS.dayDis=hh;p.remove()};var ap=()=>document.body.appendChild(p);document.body?ap():L('DOMContentLoaded',ap)}}
window.theme=()=>{var t=d.dataset.theme=='dark'?'light':'dark';
d.dataset.theme=LS.theme=t;TE()};
d.dataset.theme=LS.theme||(matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
C();L('pageshow',e=>{if(e.persisted)C()});
var k=[38,38,40,40,37,39,37,39,66,65],i=0;
L('keydown',e=>{i=e.keyCode===k[i]?i+1:0;
if(i===k.length){toggleCrt();i=0;}})
}();)JS";
#endif

void WebPortal::hThemeJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if EG_GZ_THEME_JS
  res.sendGzipP(200, FPSTR(HTTP_CT_JS), THEME_JS_GZ, THEME_JS_GZ_LEN);
#else
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(THEME_JS));
#endif
}

#define RESTART_COUNTDOWN_SECS 8
// esp8266 copies the new image at boot
#ifdef ESP8266
#define FLASH_COUNTDOWN_SECS 20
#else
#define FLASH_COUNTDOWN_SECS 10
#endif

static const char RESTART_COUNTDOWN[] PROGMEM = R"HTML(
<p class=muted id=ct></p>
<script>
var n=window._rs,c=byID('ct'),a=null,T=setTimeout,sh=t=>c.textContent=t,rl=()=>sh('Reloading in '+n+'s...');
rl();
function p(){
  if(document.hidden){T(p,1000);return}
  if(a)a.abort();
  var b=a=new AbortController();
  T(()=>b.abort(),1000);
  fetch('/ping',{cache:'no-store',signal:b.signal}).then(r=>location.href='/').catch(()=>T(p,500));
}
var t=setInterval(()=>--n>0?rl():(clearInterval(t),sh('Checking...'),p()),1000);
</script>
)HTML";

void WebPortal::sendRestartCountdown(EGHttpResponse& res) {
  char buf[40];
  int n = snprintf_P(buf, sizeof(buf), PSTR("<script>window._rs=%u;</script>"),
                     (unsigned)RESTART_COUNTDOWN_SECS);
  if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);
  res.sendChunk(FPSTR(RESTART_COUNTDOWN));
}

void WebPortal::hRestart(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Restarting"));
  res.sendChunk(F("<p>Device is restarting. This page will reload once it's back.</p>"));
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

void WebPortal::hGuard(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* e = req.arg(F("enter"));
  if (!e || strcmp_P(e, PSTR("1")) != 0) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("/guard?enter=1 restarts into safe mode"));
    return;
  }
  BootGuard::request_safe();
  hRestart(req, res, nullptr);
}

// /info

static const char IR_BYTES[] PROGMEM = "%u bytes";
static const char IR_HEX[]   PROGMEM = "0x%08x";
static const char IR_CODE[]  PROGMEM = "<code>%s</code>";
static const char IR_END[]   PROGMEM = "</table></details>";

void WebPortal::infoRow(EGHttpResponse& res, PGM_P key, PGM_P fmt, ...) {
  char row[320];
  size_t pos = 0;
  advance_pos(pos, snprintf_P(row, sizeof(row), PSTR("<tr><th>%s</th><td>"), key), sizeof(row));
  va_list ap;
  va_start(ap, fmt);
  advance_pos(pos, vsnprintf_P(row + pos, sizeof(row) - pos, fmt, ap), sizeof(row));
  va_end(ap);
  advance_pos(pos, snprintf_P(row + pos, sizeof(row) - pos, PSTR("</td></tr>")), sizeof(row));
  res.sendChunk(row, pos);
}

#define INFO_ROW(k, fmt, ...) WebPortal::infoRow(res, PSTR(k), fmt, ##__VA_ARGS__)

struct InfoNet {
  char ip[EG_IPV4_MAX + 1], gw[EG_IPV4_MAX + 1], sn[EG_IPV4_MAX + 1], dns_[EG_IPV4_MAX + 1];
  char bssid[20], mac[18];
  char ssid[6 * EGPORTAL_SSID_MAX + 1];   // escaped worst case
  char ap_ssid[EGPORTAL_SSID_MAX + 1], ap_ip[EG_IPV4_MAX + 1], ap_mac[18];
};

static void info_fmt_mac(char* dst, size_t cap, const uint8_t* m) {
  snprintf_P(dst, cap, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void info_collect_net(InfoNet& net) {
  Wifi::formatIP(net.ip,    sizeof(net.ip),    WiFi.localIP());
  Wifi::formatIP(net.gw,    sizeof(net.gw),    WiFi.gatewayIP());
  Wifi::formatIP(net.sn,    sizeof(net.sn),    WiFi.subnetMask());
  Wifi::formatIP(net.dns_,  sizeof(net.dns_),  WiFi.dnsIP());
  Wifi::formatIP(net.ap_ip, sizeof(net.ap_ip), WiFi.softAPIP());
  uint8_t macBuf[6];
  WiFi.macAddress(macBuf);
  info_fmt_mac(net.mac, sizeof(net.mac), macBuf);
  WiFi.softAPmacAddress(macBuf);
  info_fmt_mac(net.ap_mac, sizeof(net.ap_mac), macBuf);
  const uint8_t* bssidPtr = WiFi.BSSID();
  if (bssidPtr) info_fmt_mac(net.bssid, sizeof(net.bssid), bssidPtr);
  else          net.bssid[0] = '\0';
  egesc_html(net.ssid, sizeof(net.ssid), WiFi.SSID().c_str());
  strlcpy(net.ap_ssid, WiFi.softAPSSID().c_str(), sizeof(net.ap_ssid));
}

#ifdef ESP8266
static void info_render_frames(EGHttpResponse& res, const CrashDump::Snapshot* cd) {
  // code addresses only
  char framesbuf[256];
  size_t fp = 0;
  bool any = false;
  for (uint32_t i = 0; i < cd->word_count && fp < sizeof(framesbuf) - 12; i++) {
    uint32_t w = cd->stack[i];
    if (w < 0x40100000u || w >= 0x40300000u) continue;
    int wn = snprintf_P(framesbuf + fp, sizeof(framesbuf) - fp,
                        PSTR("%s0x%08x"), any ? PSTR(" ") : PSTR(""), (unsigned)w);
    if (wn > 0 && (size_t)wn < sizeof(framesbuf) - fp) fp += (size_t)wn;
    any = true;
  }
  if (any) {
    framesbuf[fp] = '\0';
    INFO_ROW("Stack frames", IR_CODE, framesbuf);
  }
}

static void info_render_reset(EGHttpResponse& res) {
  INFO_ROW("Last reset",    W_FMT_S, DeviceInfo::resetText());
  uint32_t epc1, excvaddr;
  uint8_t exccause;
  if (DeviceInfo::resetExc(&epc1, &excvaddr, &exccause)) {
    INFO_ROW("Exc cause", W_FMT_U,   (unsigned)exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)epc1);
    INFO_ROW("Exc addr",  IR_HEX, (unsigned)excvaddr);
  }
  if (const CrashDump::Snapshot* cd = CrashDump::lastCrash()) {
    INFO_ROW("EPC2",      IR_HEX, (unsigned)cd->epc2);
    INFO_ROW("EPC3",      IR_HEX, (unsigned)cd->epc3);
    INFO_ROW("DEPC",      IR_HEX, (unsigned)cd->depc);
    INFO_ROW("SP",        IR_HEX, (unsigned)cd->sp);
    info_render_frames(res, cd);
  }
}
#elif defined(ESP32)
static void info_render_backtrace(EGHttpResponse& res, const CrashDump::Snapshot* cd) {
  char framesbuf[256];
  size_t fp = 0;
  for (uint32_t i = 0; i < cd->word_count && fp < sizeof(framesbuf) - 12; i++) {
    int wn = snprintf_P(framesbuf + fp, sizeof(framesbuf) - fp,
                        PSTR("%s0x%08x"), i ? " " : "", (unsigned)cd->stack[i]);
    if (wn > 0 && (size_t)wn < sizeof(framesbuf) - fp) fp += (size_t)wn;
  }
  framesbuf[fp] = '\0';
#ifdef __riscv
  INFO_ROW("Stack",     IR_CODE, framesbuf);   // raw words, not PCs
#else
  INFO_ROW("Backtrace", IR_CODE, framesbuf);
#endif
}

static void info_render_reset(EGHttpResponse& res) {
  INFO_ROW("Last reset", W_FMT_S, DeviceInfo::resetText());
  if (const CrashDump::Snapshot* cd = CrashDump::lastCrash()) {
#ifdef __riscv
    INFO_ROW("mcause",    W_FMT_U,   (unsigned)cd->exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)cd->epc1);
    INFO_ROW("mtval",     IR_HEX, (unsigned)cd->excvaddr);
#else
    INFO_ROW("Exc cause", W_FMT_U,   (unsigned)cd->exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)cd->epc1);
    INFO_ROW("Exc addr",  IR_HEX, (unsigned)cd->excvaddr);
#endif
    if (cd->epc2) INFO_ROW("EPC2", IR_HEX, (unsigned)cd->epc2);
    if (cd->epc3) INFO_ROW("EPC3", IR_HEX, (unsigned)cd->epc3);
    if (cd->sp)   INFO_ROW("SP",   IR_HEX, (unsigned)cd->sp);
    if (cd->word_count) info_render_backtrace(res, cd);
  }
}
#endif

static void info_render_system(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>System</summary><table>"));
  INFO_ROW("Uptime",        W_FMT_S,DeviceInfo::uptimeString());
  INFO_ROW("Chip ID",       W_FMT_S,DeviceInfo::chipid());
#ifdef ESP8266
  INFO_ROW("Platform",      PSTR("esp8266"));
  INFO_ROW("Flash chip",    W_FMT_S,DeviceInfo::flashInfo());
  INFO_ROW("Flash size",    IR_BYTES,        (unsigned)ESP.getFlashChipSize());
  INFO_ROW("Real flash",    IR_BYTES,        (unsigned)ESP.getFlashChipRealSize());
  INFO_ROW("Core version",  W_FMT_S,ESP.getCoreVersion().c_str());
  INFO_ROW("Boot version",  W_FMT_U,(unsigned)ESP.getBootVersion());
#elif defined(ESP32)
  INFO_ROW("Platform",      W_FMT_S,ESP.getChipModel());
  INFO_ROW("Flash chip",    W_FMT_S,DeviceInfo::flashInfo());
  INFO_ROW("Flash size",    IR_BYTES,        (unsigned)ESP.getFlashChipSize());
  INFO_ROW("Chip rev",      W_FMT_U,(unsigned)ESP.getChipRevision());
  INFO_ROW("SDK version",   W_FMT_S,ESP.getSdkVersion());
#endif
  INFO_ROW("CPU frequency", PSTR("%u MHz"),  (unsigned)ESP.getCpuFreqMHz());
#ifdef ESP32
  {
    float t = DeviceInfo::chipTempC();
    if (!isnan(t)) {
      INFO_ROW("CPU temperature", PSTR("%.1f \xC2\xB0""C"), t);
    }
  }
#endif
  INFO_ROW("Free heap",     IR_BYTES,        (unsigned)DeviceInfo::freeHeap());
  INFO_ROW("Sketch size",   PSTR("%u / %u bytes"), (unsigned)ESP.getSketchSize(),
                                             (unsigned)(ESP.getSketchSize() + ESP.getFreeSketchSpace()));
  info_render_reset(res);
  INFO_ROW("Safe mode", W_FMT_S, BootGuard::safe_mode() ? "yes" : "no");
  INFO_ROW("Crash boots in a row", W_FMT_U, (unsigned)BootGuard::crash_streak());
  res.sendChunk(FPSTR(IR_END));
}

static void info_render_wifi(EGHttpResponse& res, const InfoNet& net) {
  res.sendChunk(F("<details open><summary>WiFi</summary><table>"));
  INFO_ROW("Connected",      W_FMT_S,     WiFi.isConnected() ? PSTR("Yes") : PSTR("No"));
  INFO_ROW("Station SSID",   W_FMT_S,     net.ssid);
  INFO_ROW("Station IP",     W_FMT_S,     net.ip);
  INFO_ROW("Station gateway",W_FMT_S,     net.gw);
  INFO_ROW("Station subnet", W_FMT_S,     net.sn);
  INFO_ROW("DNS Server",     W_FMT_S,     net.dns_);
  INFO_ROW("Hostname",       W_FMT_S,     DeviceInfo::hostname());
  INFO_ROW("Station MAC",    W_FMT_S,     net.mac);
  INFO_ROW("RSSI",           PSTR("%d dBm"), (int)Wifi::rssi);
  INFO_ROW("BSSID",          W_FMT_S,     net.bssid);
  INFO_ROW("Channel",        W_FMT_D,  (int)WiFi.channel());
  {
    int q = Wifi::txPowerQdbm();
    if (q >= 0) INFO_ROW("TX power", PSTR("%d.%02d dBm"), q / 4, (q % 4) * 25);
  }
  INFO_ROW("PHY mode",       W_FMT_S,     Wifi::phyModeStr());
  INFO_ROW("Radio sleep",    W_FMT_S,     Wifi::sleepModeStr());
  INFO_ROW("Autoconnect",    W_FMT_S,     WiFi.getAutoReconnect() ? PSTR("Enabled") : PSTR("Disabled"));
  INFO_ROW("AP SSID",        W_FMT_S,     net.ap_ssid[0] ? net.ap_ssid : PSTR("(not active)"));
  INFO_ROW("AP IP",          W_FMT_S,     net.ap_ip);
  INFO_ROW("AP MAC",         W_FMT_S,     net.ap_mac);
  res.sendChunk(FPSTR(IR_END));
}

static void info_render_modules(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>Modules</summary><ul style='columns:2;list-style:none;padding:0'>"));
  {
    char li[48];
    uint8_t count = EGModuleRegistry::count();
    for (uint8_t i = 0; i < count; i++) {
      EGModule* m = EGModuleRegistry::get(i);
      if (m == nullptr) continue;
      int n = snprintf_P(li, sizeof(li), PSTR("<li>%s</li>"), m->name());
      if (n > 0 && (size_t)n < sizeof(li)) res.sendChunk(li, (size_t)n);
    }
    res.sendChunk(F("<li>portal</li>"));
  }
  res.sendChunk(F("</ul></details>"));
}

static void info_render_about(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>About</summary><table>"));
  INFO_ROW("Version",      PSTR(RELEASE_VERSION));
  INFO_ROW("Git",          PSTR(GIT_VERSION));
  INFO_ROW("Build env",    PSTR(BUILD_ENV));
  INFO_ROW("Build date",   PSTR(__DATE__ " " __TIME__));
  AppHooks::info_rows(res);
  res.sendChunk(FPSTR(IR_END));
}

#undef INFO_ROW

void WebPortal::hInfo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Info"));

  InfoNet net;
  info_collect_net(net);

  if (net.ssid[0]) res.sendKV(F("<p class=muted>Connected to <b>"), net.ssid);
  else             res.sendChunk(F("<p class=muted>Connected to <b>(not connected)"));
  res.sendKV(F("</b> with IP <b>"), net.ip);
  res.sendKV(F("</b> &middot; "), DeviceInfo::chipmodel(), F("</p>"));

  info_render_system(res);
  info_render_wifi(res, net);
  info_render_modules(res);
  info_render_about(res);

  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void about_modules(EGHttpResponse& res) {
  uint8_t count = EGModuleRegistry::count();
  for (uint8_t i = 0; i < count; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (m == nullptr) continue;
    char modbuf[40];
    int mn = snprintf_P(modbuf, sizeof(modbuf),
      PSTR("%s\"%s\""), (i > 0 ? PSTR(",") : PSTR("")), m->name());
    size_t mlen = 0;
    advance_pos(mlen, mn, sizeof(modbuf));
    if (mlen) res.sendChunk(modbuf, mlen);
  }
}

void WebPortal::hAbout(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  uint8_t macb[6];
  WiFi.macAddress(macb);
  char mac_str[13];
  snprintf_P(mac_str, sizeof(mac_str), PSTR("%02X%02X%02X%02X%02X%02X"),
    macb[0], macb[1], macb[2], macb[3], macb[4], macb[5]);

  char buf[320];
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("{\"ver\":\"" RELEASE_VERSION "\",\"git\":\"" GIT_VERSION "\",\"env\":\"" BUILD_ENV "\","
         "\"date\":\"" __DATE__ " " __TIME__ "\",\"chip\":\"%s\",\"flash\":\"%s\",\"mac\":\"%s\","
         "\"host\":\"%s\","),
    DeviceInfo::chipmodel(),
    DeviceInfo::flashInfo(),
    mac_str,
    DeviceInfo::hostname());
  size_t len = 0;
  advance_pos(len, n, sizeof(buf));
  if (len) res.sendChunk(buf, len);

  AppHooks::about_json(res);
  res.sendChunk(F("\"modules\":["));
  about_modules(res);
  res.sendChunk(F("]}"));
  res.endChunked();
}

void WebPortal::hReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  s_pendingFactoryReset = true;
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Reset"));
  res.sendChunk(F("<p>All settings cleared. Device is restarting and will return to the setup portal.</p>"));
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

void WebPortal::hEraseWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // back to DHCP for the setup AP
  Log::console(PSTR("WebPortal: erasing WiFi credentials"));
  EGPrefs::put("net", EG_K_STATIC_IP, "0");
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Erase WiFi"));
  res.sendChunk(F("<p>WiFi credentials erased and network reset to DHCP. Device will reboot into Setup Mode.</p>"));
  WebPortal::sendPageTail(res);
  res.endChunked();
  EGPrefs::commit();
  s_pendingEraseCreds = true;
  WebPortal::requestRestart(1500);
}

// /wifi + /wifisave

static const char WIFI_FORM_HEAD[] PROGMEM = R"HTML(
<form method=POST action=/wifisave>
<label style='display:flex;align-items:center;justify-content:space-between'>
  <span>Network</span>
  <button type=button id=rescan class='btn-sm btn-o'>Rescan</button>
</label>
<select id=ws onchange="byID('s').value=this.options[this.selectedIndex].dataset.ssid||''">
<option value=''>Scanning&hellip;</option>
</select>
<label for=s>SSID</label>
<input id=s name=ssid maxlength=32 placeholder='(blank to keep current network)'>
<label for=p>Password</label>
<input id=p name=password type=password maxlength=64 placeholder='(blank for open networks)'>
)HTML";

static const char WIFI_FORM_END[] PROGMEM = R"HTML(
<button type=submit>Save and reboot</button>
</form>
<p class=muted style='margin-top:1em'>If the new network is unreachable, the previous one is restored automatically after about a minute.</p>
)HTML";

static void wifi_ip_field(EGHttpResponse& res, const __FlashStringHelper* head,
                          const char* prefKey) {
  static const char IPV4_PATTERN_ATTR[] PROGMEM =
    " pattern='^(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)(\\.(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)){3}$' value='";
  char esc[6 * EG_IPV4_MAX + 1];
  egesc_html(esc, sizeof(esc), EGPrefs::getString("net", prefKey));
  res.sendChunk(head);
  res.sendChunk(FPSTR(IPV4_PATTERN_ATTR));
  res.sendKV(nullptr, esc, F("'>"));
}

static void wifi_render_static_ip(EGHttpResponse& res) {
  bool useStatic = EGPrefs::getBool("net", EG_K_STATIC_IP);
  res.sendChunk(F("<details"));
  if (useStatic) res.sendChunk(F(" open"));
  res.sendChunk(F("><summary>Network (advanced)</summary>"
                  "<label style='display:flex;align-items:center;gap:.5em'>"
                  "<input type=checkbox name=static_ip value=1"));
  if (useStatic) res.sendChunk(FPSTR(W_CHECKED));
  res.sendChunk(F(">Use static IP (otherwise DHCP)</label>"));

  wifi_ip_field(res, F("<label for=ip>IP address</label>"
                       "<input id=ip name=ip maxlength=15 placeholder='192.168.1.50'"), EG_K_IP);
  wifi_ip_field(res, F("<label for=gw>Gateway</label>"
                       "<input id=gw name=gw maxlength=15 placeholder='192.168.1.1'"), EG_K_GW);
  wifi_ip_field(res, F("<label for=sn>Subnet mask</label>"
                       "<input id=sn name=sn maxlength=15 placeholder='255.255.255.0'"), EG_K_SN);
  wifi_ip_field(res, F("<label for=dns>DNS server <span class=muted style='font-weight:normal'>(optional)</span></label>"
                       "<input id=dns name=dns maxlength=15 placeholder='same as gateway'"), EG_K_DNS);
  res.sendChunk(FPSTR(W_DETAILS_END));
}

void WebPortal::hWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Network"));

  char ssid[6 * EGPORTAL_SSID_MAX + 1];
  egesc_html(ssid, sizeof(ssid), WiFi.SSID().c_str());
  if (ssid[0]) res.sendKV(F("<p class=muted>Connected to <b>"), ssid, F("</b></p>"));
  else         res.sendChunk(F("<p class=muted>Connected to <b>(none)</b></p>"));

  res.sendChunk(FPSTR(WIFI_FORM_HEAD));
  wifi_render_static_ip(res);
  res.sendChunk(FPSTR(WIFI_FORM_END));

  if (!EGModuleRegistry::quarantined(&ntpclient)) {
    // lazy load the tz list
    res.sendChunk(F("<details style='margin-top:1.5em' "
                    "ontoggle=\"if(this.open&&!this.dataset.loaded){"
                    "this.dataset.loaded=1;"
                    "var s=document.createElement('script');"
                    "s.src='/ntpjs" EG_CACHE_BUST "';this.appendChild(s);"
                    "}\">"
                    "<summary>Time / NTP</summary>"));
    ntpclient.renderInlineForm(res);
    res.sendChunk(FPSTR(W_DETAILS_END));
  }

  res.sendChunk(FPSTR(wifiAdvancedHTML));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

void WebPortal::hWifiScan(EGHttpRequest& req, EGHttpResponse& res, void*) {
  static uint32_t s_lastScan = 0;
  static const char SCANNING[] PROGMEM = "{\"state\":\"scanning\"}";
  bool force = req.arg(F("refresh")) != nullptr;

  int status = WiFi.scanComplete();
  if (!force && status == WIFI_SCAN_RUNNING) {
    res.send(200, FPSTR(HTTP_CT_JSON), FPSTR(SCANNING));
    return;
  }

  bool stale = (fast_millis() - s_lastScan) > 60000UL;
  bool needScan = force || status <= 0 || stale;
  if (needScan) {
    if (status != WIFI_SCAN_RUNNING) WiFi.scanDelete();
    WiFi.scanNetworks(true /*async*/, false /*show_hidden*/);
    s_lastScan = fast_millis();
    res.send(200, FPSTR(HTTP_CT_JSON), FPSTR(SCANNING));
    return;
  }

  EGPortal::ScanEntry aps[12];
  size_t count = EGPortal::processScan(aps, 12, status);

  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(F("{\"state\":\"complete\",\"aps\":["));
  // row[0] is the comma, skipped on the first
  char row[240];
  bool first = true;
  for (size_t i = 0; i < count; i++) {
#ifdef ESP8266
    int open = (aps[i].enc == ENC_TYPE_NONE) ? 1 : 0;
#else
    int open = (aps[i].enc == WIFI_AUTH_OPEN) ? 1 : 0;
#endif
    row[0] = ',';
    EGJson j(row + 1, sizeof(row) - 1);
    j.map_begin();
    j.str(PSTR("ssid"), aps[i].ssid);
    j.sint(PSTR("rssi"), aps[i].rssi);
    j.uint(PSTR("open"), open);
    j.map_end();
    if (j.overflow) continue;
    if (first) res.sendChunk(row + 1, j.length());
    else res.sendChunk(row, j.length() + 1);
    first = false;
  }
  res.sendChunk(F("]}"));
  res.endChunked();
}

static void wifi_render_save_msg(EGHttpResponse& res, bool ssidProvided) {
  if (ssidProvided) {
    char ssid[6 * EGPORTAL_SSID_MAX + 1];
    egesc_html(ssid, sizeof(ssid), Wifi::heldSsid());
    res.sendKV(F("<p>Saving credentials for <b>"), ssid, F("</b> and rebooting.</p>"));
    res.sendChunk(F("<p class=muted>If the new network is reachable, the device will "
                    "reconnect on its own. If it isn't, the previous credentials are "
                    "restored automatically after about a minute.</p>"));
  } else {
    res.sendChunk(F("<p>Saving network settings and rebooting.</p>"
                    "<p class=muted>WiFi credentials unchanged. New IP "
                    "configuration will apply once the device comes back up.</p>"));
  }
}

// too long is refused, cut "1.2.3.2550" passes as .255
// checked with static off too, /network shows them
static bool take_ip(EGHttpRequest& req, const __FlashStringHelper* name,
                    char (&dst)[EG_IPV4_MAX + 1]) {
  const char* v = req.arg(name);
  if (!v) v = "";
  if (strlcpy(dst, v, sizeof(dst)) >= sizeof(dst)) return false;
  IPAddress a;
  return !*dst || a.fromString(dst);
}

// arg() reuses its buffer, copy the ssid first
static bool wifi_hold_args(EGHttpRequest& req, const char* ssid) {
  char s[EGPORTAL_SSID_MAX + 1];
  if (strlcpy(s, ssid, sizeof(s)) >= sizeof(s)) return false;
  return Wifi::holdCreds(s, req.arg(F("password")));
}

void WebPortal::hWifiSave(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // write all four, keeps values on toggle
  bool wantStatic = (req.arg(FPSTR(EG_K_STATIC_IP)) != nullptr);
  char ip_buf[EG_IPV4_MAX + 1], gw_buf[EG_IPV4_MAX + 1];
  char sn_buf[EG_IPV4_MAX + 1], dns_buf[EG_IPV4_MAX + 1];
  if (!take_ip(req, FPSTR(EG_K_IP),  ip_buf)
      || !take_ip(req, FPSTR(EG_K_GW),  gw_buf)
      || !take_ip(req, FPSTR(EG_K_SN),  sn_buf)
      || !take_ip(req, FPSTR(EG_K_DNS), dns_buf)) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad network config: not an IP address\nClick Back to fix it.\n"));
    return;
  }

  if (wantStatic) {
    char err[96];
    if (!Wifi::validateStatic(ip_buf, gw_buf, sn_buf, err, sizeof(err))) {
      char body[160];
      int n = snprintf_P(body, sizeof(body),
        PSTR("Bad network config: %s\nClick Back to fix it.\n"), err);
      if (n > 0) res.send(400, FPSTR(HTTP_CT_PLAIN), body);
      else       res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad network config"));
      return;
    }
  }

  const char* ssid = req.arg(F("ssid"));
  bool ssidProvided = (ssid && *ssid);
  if (ssidProvided && !wifi_hold_args(req, ssid)) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad WiFi settings: SSID or password too long\nClick Back to fix it.\n"));
    return;
  }

  // backup first, boot can revert
  Wifi::saveNetBackup();
  EGPrefs::put("net", EG_K_STATIC_IP, wantStatic ? "1" : "0");
  EGPrefs::put("net", EG_K_IP, ip_buf);
  EGPrefs::put("net", EG_K_GW, gw_buf);
  EGPrefs::put("net", EG_K_SN, sn_buf);
  EGPrefs::put("net", EG_K_DNS, dns_buf);
  EGPrefs::commit();

  if (ssidProvided) {
    Wifi::saveBackupCreds();
    s_pendingWifiSave = true;
  }

  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Saving WiFi"));
  wifi_render_save_msg(res, ssidProvided);
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();

  WebPortal::requestRestart(2500);
}

// /update

static const char UPDATE_FORM_BODY[] PROGMEM = R"HTML(
<p>Upload a firmware <code>.bin</code> built for this board. The device will reboot when the upload completes.</p>
<form id=uf>
<input type=file id=uf_file accept=".bin,application/octet-stream" required>
<div id=uf_check class=a style=display:none></div>
<button type=submit id=uf_btn disabled>Upload</button>
</form>
<progress id=uf_p style="width:100%;margin-top:.6em;display:none" value=0 max=1></progress>
<p id=uf_st style="margin-top:.6em"></p>
<script>
(function(){
var D=window._dev||{},F=byID('uf_file'),B=byID('uf_btn'),C=byID('uf_check'),S=byID('uf_st'),P=byID('uf_p');
function sC(l,h){C.style.display='block';C.className='a a'+l;C.innerHTML=h}
async function cF(f){
  if(!f){B.disabled=true;C.style.display='none';return}
  var u=new Uint8Array(await f.arrayBuffer());
  if(u.length<8||(u[0]!==0xE9&&u[0]!==0xEA)){B.disabled=true;sC('e','Not an ESP firmware image.');return}
  var t=new TextDecoder('latin1').decode(u),e=D.env||'';
  B.disabled=false;
  if(D.tag&&t.indexOf(D.tag)!==-1)sC('k','<b>Looks compatible</b> - <code>'+e+'</code> matches.');
  else sC('w','<b>Warning:</b> running <code>'+e+'</code> not found in file. Upload anyway?');
}
F.addEventListener('change',function(){cF(this.files[0])});
byID('uf').addEventListener('submit',function(e){
  e.preventDefault();
  var f=F.files[0];if(!f)return;
  B.disabled=true;
  S.textContent='Uploading '+f.name+' ('+f.size+' bytes)...';
  P.style.display='block';P.value=0;
  var x=new XMLHttpRequest();
  x.open('POST','/update',!0);
  x.setRequestHeader('Content-Type','application/octet-stream');
  var dv=0;
  x.upload.onprogress=function(v){if(!dv&&v.lengthComputable)P.value=v.loaded/v.total};
  x.upload.onload=function(){if(!dv){P.removeAttribute('value');S.textContent='Sent, device still writing...'}};
  var pt=setInterval(function(){
    fetch('/update/progress',{cache:'no-store'}).then(r=>r.json()).then(j=>{if(j.total){dv=1;P.value=j.done/j.total}}).catch(()=>{});
  },1000);
  x.onloadend=function(){clearInterval(pt)};
  x.onload=function(){
    if(x.status===200){
      P.value=1;
      var R=x.responseText;
      var n=D.rs;
      function sh(t){S.innerHTML='<b style="color:#0a0"></b><br>'+t;S.firstChild.textContent=R}
      sh('Reloading in '+n+'s...');
      var pa=null,T=setTimeout;
      function pn(){
        if(document.hidden){T(pn,1000);return}
        if(pa)pa.abort();
        var b=pa=new AbortController();
        T(()=>b.abort(),1000);
        fetch('/ping',{cache:'no-store',signal:b.signal}).then(r=>location.href='/').catch(()=>T(pn,500));
      }
      var iv=setInterval(function(){
        if(--n>0)sh('Reloading in '+n+'s...');
        else{clearInterval(iv);sh('Checking...');pn()}
      },1000);
    }else{
      B.disabled=false;
      S.innerHTML='<b style="color:#c0392b"></b>';S.firstChild.textContent='Upload failed: '+x.responseText;
    }
  };
  x.onerror=function(){
    B.disabled=false;
    S.innerHTML='<b style="color:#c0392b">Upload error (connection lost?)</b>';
  };
  x.send(f);
});
})();
</script>
)HTML";

// BUILD_ENV alone matches other variants
// keep it used, gc-sections drops it
static const char EG_ENV_TAG[] PROGMEM = "<EGENV:" BUILD_ENV ":ENV>";

void WebPortal::hUpdateForm(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Update"));
  char dev[140];
  int n = snprintf_P(dev, sizeof(dev),
    PSTR("<script>window._dev={env:\"" BUILD_ENV "\",tag:\"%s\",rs:%u};</script>"),
    EG_ENV_TAG, (unsigned)FLASH_COUNTDOWN_SECS);
  if (n > 0 && (size_t)n < sizeof(dev)) res.sendChunk(dev, (size_t)n);
  res.sendChunk(FPSTR(UPDATE_FORM_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

// one upload at a time
static struct {
  size_t expected = 0;
  size_t delivered = 0;
  bool   write_error = false;
  uint8_t head[8];         // held until entry addr checked
  uint8_t head_n = 0;
  bool    started = false; // OTA_FAILED owed
} s_ota;

static void ota_begin(size_t expected) {
  s_ota.expected    = expected;
  s_ota.delivered   = 0;
  s_ota.write_error = false;
  s_ota.head_n      = 0;
  s_ota.started     = false;
  uint32_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  if (!Update.begin(s_ota.expected ? s_ota.expected : maxSketch)) {
    Log::console(PSTR("OTA: Update.begin failed (size=%u, free=%u)"),
                 (unsigned)s_ota.expected, (unsigned)maxSketch);
    s_ota.write_error = true;
  } else {
    Log::console(PSTR("OTA: starting (%u bytes)"), (unsigned)s_ota.expected);
    s_ota.started = true;
    Lifecycle::send(Lifecycle::OTA_START);
  }
}

static void ota_failed() {
  if (!s_ota.started) return;
  s_ota.started = false;
  Lifecycle::send(Lifecycle::OTA_FAILED);
}

static bool ota_entry_ok(uint32_t entry) {
#ifdef ESP8266
  // esp8266 IRAM
  bool plat_ok = (entry >= 0x40100000UL && entry < 0x40200000UL);
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  // S3 IRAM
  bool plat_ok = (entry >= 0x40370000UL && entry < 0x403E0000UL);
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  bool plat_ok = (entry >= 0x40020000UL && entry < 0x40050000UL);
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  bool plat_ok = (entry >= 0x40380000UL && entry < 0x403E0000UL);
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  bool plat_ok = (entry >= 0x40800000UL && entry < 0x40880000UL);
#else
  // esp32 IRAM
  bool plat_ok = (entry >= 0x40080000UL && entry < 0x400A0000UL);
#endif
  return plat_ok;
}

static bool ota_write(const uint8_t* p, size_t len) {
  size_t wrote = Update.write((uint8_t*)p, len);
  if (wrote != len) {
    Log::console(PSTR("OTA: short write %u/%u err=%u"),
                 (unsigned)wrote, (unsigned)len, Update.getError());
    s_ota.write_error = true;
    return false;
  }
  s_ota.delivered += wrote;
  return true;
}

static void ota_data(const char* chunk, size_t len) {
  if (s_ota.write_error || !Update.isRunning()) return;  // already failed, drain
  // check entry_addr first, wrong chip bricks it
  if (s_ota.head_n < sizeof(s_ota.head)) {
    size_t n = sizeof(s_ota.head) - s_ota.head_n;
    if (n > len) n = len;
    memcpy(s_ota.head + s_ota.head_n, chunk, n);
    s_ota.head_n += n;
    chunk += n;
    len -= n;
    if (s_ota.head_n < sizeof(s_ota.head)) return;
    const uint8_t* h = s_ota.head;
    uint32_t entry = (uint32_t)h[4] | ((uint32_t)h[5] << 8)
                   | ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
    if (!ota_entry_ok(entry)) {
      Log::console(PSTR("OTA: REFUSING wrong-platform bin (entry=0x%08x)"),
                   (unsigned)entry);
      Update.end(false);
      s_ota.write_error = true;
      return;
    }
    if (!ota_write(h, sizeof(s_ota.head))) return;
  }
  if (len) ota_write((const uint8_t*)chunk, len);
}

static void ota_end() {
  // Update.end() is strict, needs full size
  if (s_ota.write_error || !Update.isRunning()) {
    Log::console(PSTR("OTA: aborting on prior error"));
    if (Update.isRunning()) Update.end(false);  // discard
    s_ota.write_error = true;
    return;
  }
  if (s_ota.delivered != s_ota.expected) {
    Log::console(PSTR("OTA: byte-count mismatch %u != %u"),
                 (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
    Update.end(false);
    s_ota.write_error = true;
    return;
  }
  if (Update.end()) {
    Log::console(PSTR("OTA: success (%u bytes), restarting"),
                 (unsigned)s_ota.delivered);
    // here too, reply may never go out
    WebPortal::requestRestart(1500);
  } else {
    Log::console(PSTR("OTA: end failed err=%u"), Update.getError());
    s_ota.write_error = true;
  }
}

static void ota_abort() {
  if (Update.isRunning()) Update.end(false);
  s_ota.write_error = true;
  Log::console(PSTR("OTA: aborted at %u/%u bytes"),
               (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
}

void WebPortal::hUpdateBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* chunk, size_t len, void*) {
  // main loop, flash write ok
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: ota_begin(req.bodyLen()); break;
    case EGHttpServer::BODY_DATA:  ota_data(chunk, len);     break;
    case EGHttpServer::BODY_END:
      ota_end();
      if (s_ota.write_error) ota_failed();
      break;
    case EGHttpServer::BODY_ABORT: ota_abort(); ota_failed(); break;
  }
}

void WebPortal::hUpdateDone(EGHttpRequest& req, EGHttpResponse& res, void*) {
  if (s_ota.write_error || Update.hasError()) {
    char errBuf[96];
    snprintf_P(errBuf, sizeof(errBuf),
               PSTR("Update failed: %u/%u bytes, err=%u"),
               (unsigned)s_ota.delivered, (unsigned)s_ota.expected,
               (unsigned)Update.getError());
    res.send(500, FPSTR(HTTP_CT_PLAIN), errBuf);
    return;
  }
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
  WebPortal::requestRestart(1500);
}

// device-side count, 0 when idle
void WebPortal::hUpdateProgress(EGHttpRequest& req, EGHttpResponse& res, void*) {
  char buf[48];
  snprintf_P(buf, sizeof(buf), PSTR("{\"done\":%u,\"total\":%u}"),
             (unsigned)s_ota.delivered, (unsigned)(s_ota.started ? s_ota.expected : 0));
  res.addHeader(FPSTR(HTTP_H_CACHE), F("no-store"));
  res.send(200, FPSTR(HTTP_CT_JSON), buf);
}

// /param

#define APPEND_LIT(buf, pos, cap, lit) do {                              \
  size_t _n = sizeof(lit) - 1;                                           \
  if ((pos) + _n < (cap)) { memcpy_P((buf)+(pos), PSTR(lit), _n); (pos) += _n; } \
} while (0)

// POST body for /param and /import
struct BodyBuf {
  char*  buf;
  size_t len;
  size_t cap;
  bool   ok;
};

static void body_free(BodyBuf& b) {
  if (b.buf) { free(b.buf); b.buf = nullptr; }
  b.len = 0;
  b.cap = 0;
  b.ok  = false;
}

// over 8 KB is dropped
static bool body_size_ok(size_t cap) {
  return cap != 0 && cap <= 8192;
}

static bool body_alloc(BodyBuf& b, size_t cap) {
  b.buf = (char*)malloc(cap + 1);
  if (!b.buf) return false;
  b.cap = cap;
  b.ok  = true;
  return true;
}

static void body_append(BodyBuf& b, const char* chunk, size_t len) {
  if (b.ok && b.buf && b.len + len <= b.cap) {
    memcpy(b.buf + b.len, chunk, len);
    b.len += len;
  }
}

static void body_end(BodyBuf& b) {
  if (b.buf) b.buf[b.len] = '\0';
}

// freed in hParam
static BodyBuf s_paramBody;

static void param_body_begin(size_t cap) {
  body_free(s_paramBody);
  if (!body_size_ok(cap)) {
    Log::console(PSTR("/param: body size %u out of range"), (unsigned)cap);
    return;
  }
  if (!body_alloc(s_paramBody, cap)) {
    Log::console(PSTR("/param: malloc(%u) failed"), (unsigned)(cap + 1));
  }
}

void WebPortal::hParamBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                            const char* chunk, size_t len, void*) {
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: param_body_begin(req.bodyLen());         break;
    case EGHttpServer::BODY_DATA:  body_append(s_paramBody, chunk, len);    break;
    case EGHttpServer::BODY_END:   body_end(s_paramBody);                   break;
    case EGHttpServer::BODY_ABORT: body_free(s_paramBody);                  break;
  }
}

// first field a save refused, shown above the form
struct Refusal {
  char name[2 * EGPREF_ID_MAX + 2];   // mid.id
  const char* why;                    // PROGMEM
};

static void param_put(const char* mid, const char* id, const char* name,
                      const char* v, Refusal& r) {
  if (EGPrefs::put(mid, id, v) || r.why || !EGPrefs::refused()) return;
  strncpy(r.name, name, sizeof(r.name) - 1);
  r.name[sizeof(r.name) - 1] = '\0';
  r.why = EGPrefs::refused();
}

static void param_save_pref(const char* mid, const EGPref& p,
                            const char* body, size_t bodyLen, Refusal& r) {
  if (p.flags & (EGP_HIDDEN | EGP_READONLY)) return;
  if (p.type == EGP_LABEL || p.type == EGP_HEADER) return;
  char name[2 * EGPREF_ID_MAX + 2];   // mid.id
  char id_buf[EGPREF_ID_MAX + 1];
  char field[EGPREFS_MAX_VALUE + 1];
  strncpy_P(id_buf, p.id, sizeof(id_buf) - 1);
  id_buf[sizeof(id_buf) - 1] = '\0';
  snprintf_P(name, sizeof(name), PSTR("%s.%s"), mid, id_buf);

  if (p.type == EGP_BOOL) {
    const char* one = EGHttpRequest::decodeArg(body, bodyLen, name, field, sizeof(field)) ? "1" : "0";
    EGPrefs::put(mid, id_buf, one);
    return;
  }
  const char* v = EGHttpRequest::decodeArg(body, bodyLen, name, field, sizeof(field));
  if (!v) return;
  if (p.flags & EGP_SENSITIVE) {
    if (strcmp_P(v, PSTR("__CLEAR__")) == 0) {
      EGPrefs::put(mid, id_buf, "");
      return;
    }
    if (v[0] == '\0') return;
  }
  param_put(mid, id_buf, name, v, r);
}

// false = nothing saved
static bool param_save_body(uint8_t tab, Refusal& r) {
  const char* body    = s_paramBody.buf;
  size_t      bodyLen = s_paramBody.len;
  bool        ok      = s_paramBody.ok && body;

  if (ok) {
    for (size_t gi = 0; gi < EGPrefs::group_count(); gi++) {
      const EGPrefGroup* g = EGPrefs::group_at(gi);
      if (!g) continue;
      // this tab only, else other bools clear
      if (g->category.get() != tab) continue;
      // not on the page, a missing bool would save as off
      EGModule* m = EGPrefs::module_at(gi);
      if (m && m->display_order() == 0) continue;
      for (size_t j = 0; j < g->count.get(); j++) {
        const EGPref p = g->prefs.row(j);
        param_save_pref(g->module_id, p, body, bodyLen, r);
      }
    }
  }
  body_free(s_paramBody);
  return ok;
}

static void param_render_tabs(EGHttpResponse& res, uint8_t tab) {
  static const char TAB_NAMES[5][7] PROGMEM = {"System", "Input", "Output", "Upload", "Backup"};
  res.sendChunk(F("<nav class=tabs>"));
  char nav[96];
  for (uint8_t t = 0; t < 5; t++) {
    int nn = snprintf_P(nav, sizeof(nav),
      PSTR("<a href='/param?t=%u'%s>%s</a>"),
      (unsigned)t, t == tab ? PSTR(" class=ac") : PSTR(""), TAB_NAMES[t]);
    if (nn > 0 && (size_t)nn < sizeof(nav)) res.sendChunk(nav, (size_t)nn);
  }
  res.sendChunk(F("</nav>"));
}

static void param_render_backup(EGHttpResponse& res) {
  res.sendChunk(F(
    "<form method=POST action=/import onsubmit=\"return confirm('Import config and reboot?')\">"
    "<p><textarea name=blob id=cfgB rows=5 style=\"width:100%;font-family:monospace;font-size:.85em\" placeholder=\"Click Export to read current config, or paste a blob here to import\" required></textarea></p>"
    "<p><button type=button onclick=\"fetch('/export').then(r=>r.text()).then(t=>{var e=document.getElementById('cfgB');e.value=t;e.select();})\">Export</button>"
    " <button type=submit>Import</button></p></form>"
    "<p style=\"color:var(--muted);font-size:.85em\">WiFi and network settings are preserved. Device reboots automatically after import.</p>"
    "<p class=\"a aw\" style=\"font-size:.85em\"><b>Note:</b> The exported blob contains private information. Keep it private.</p>"));
}

static bool param_group_on(uint8_t gi) {
  EGModule* m = EGPrefs::module_at(gi);
  if (m && !m->hw_present()) return false;
  const EGPrefGroup* g = EGPrefs::group_at(gi);
  return !g || EGPrefs::group_enabled(g);
}

static uint8_t param_group_order(uint8_t gi) {
  EGModule* m = EGPrefs::module_at(gi);
  return m ? m->display_order() : 50;
}

static void param_sort_groups(uint8_t* order, uint8_t gc) {
  for (uint8_t i = 0; i < gc; i++) order[i] = i;
  for (uint8_t i = 1; i < gc; i++) {
    uint8_t cur = order[i];
    uint8_t o_cur = param_group_order(cur);
    bool en_cur = param_group_on(cur);
    uint8_t j = i;
    while (j > 0) {
      uint8_t o_prev = param_group_order(order[j - 1]);
      bool en_prev = param_group_on(order[j - 1]);
      bool prev_first = (en_prev != en_cur) ? en_prev : (o_prev <= o_cur);
      if (prev_first) break;
      order[j] = order[j - 1];
      j--;
    }
    order[j] = cur;
  }
}

struct ParamCtx {
  EGHttpResponse* res;
  const char* mid;
  const char* cur;
  PGM_P       disabled;
  size_t      pos;
  bool        has_lat;
  bool        has_lon;
  const EGPrefShow* show;
  char        buf[512];
  char        s_id[EGPREF_ID_MAX + 1], s_lbl[80], s_help[120], s_pat[EGPREF_PATTERN_MAX + 1];
  char        s_show[2 * EGPREF_ID_MAX + 32];   // data-w='mid.key' data-m=N
};

static const char* flash_to_ram(const char* fp, char* dst, size_t cap) {
  if (!fp) { dst[0] = '\0'; return dst; }
  strncpy_P(dst, fp, cap - 1);
  dst[cap - 1] = '\0';
  return dst;
}

static void param_appendf(ParamCtx& c, PGM_P fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf_P(c.buf + c.pos, sizeof(c.buf) - c.pos, fmt, ap);
  va_end(ap);
  advance_pos(c.pos, n, sizeof(c.buf));
}

// for the form script, "" = always shown
static const char* show_attr(ParamCtx& c, const char* id) {
  EGPrefShow r;
  c.s_show[0] = '\0';
  if (EGPrefs::show_rule(c.show, id, &r)) {
    snprintf_P(c.s_show, sizeof(c.s_show), PSTR(" data-w='%s' data-m=%u"),
               r.when, (unsigned)r.values);
  }
  return c.s_show;
}

static void param_group_header(ParamCtx& c, const EGPrefGroup* g, EGModule* mod) {
  bool has_toggle = (g->enable_key != nullptr);
  bool grp_on = EGPrefs::group_enabled(g);
  bool hw_absent = (mod && !mod->hw_present());
  const char* badge = hw_absent ? PSTR(" <span class='gb off'>not detected</span>")
                      : !has_toggle ? PSTR("")
                      : grp_on ? PSTR(" <span class='gb on'>on</span>")
                               : PSTR(" <span class='gb off'>off</span>");
  int n = snprintf_P(c.buf, sizeof(c.buf),
                     PSTR("<details class='bx%s'%s><summary>%s%s</summary>"),
                     (hw_absent || (has_toggle && !grp_on)) ? PSTR(" off") : PSTR(""),
                     show_attr(c, nullptr), g->label, badge);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.res->sendChunk(c.buf, (size_t)n);
}

static void param_emit_locator(ParamCtx& c) {
  int n = snprintf_P(c.buf, sizeof(c.buf), PSTR(
    "<p><button type=button onclick=\"window.open('" APP_LOC_URL "/','espgeiger-loc')\">Find location</button></p>"
    "<script>window.addEventListener('message',function(e){"
    "if(e.origin!=='" APP_LOC_URL "')return;"
    "var d=e.data;if(!d||d.type!=='espgeiger-location')return;"
    "var la=byID('%s.lat'),lo=byID('%s.lon');"
    "if(la&&typeof d.lat==='number')la.value=d.lat;"
    "if(lo&&typeof d.lon==='number')lo.value=d.lon;});</script>"),
    c.mid, c.mid);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.res->sendChunk(c.buf, (size_t)n);
}

static void param_emit_bool(ParamCtx& c, const char* lbl, bool inline_bool) {
  PGM_P checked = (c.cur[0] == '1') ? W_CHECKED : PSTR("");
  if (inline_bool) {
    param_appendf(c,
      PSTR("<label class='il' title='%s'><input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s>%s</label>"),
      c.s_help, c.mid, c.s_id, c.mid, c.s_id, checked, c.disabled, lbl);
  } else {
    param_appendf(c,
      PSTR("<input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s><br>"),
      c.mid, c.s_id, c.mid, c.s_id, checked, c.disabled);
  }
}

// sent per option, a long list can't overflow buf
static void param_emit_options(ParamCtx& c, const EGPref& p) {
  long cur_idx = strtol(c.cur, nullptr, 10);
  long def_idx = p.default_val ? strtol(p.default_val, nullptr, 10) : -1;
  const char* opt = c.s_pat;
  long idx = p.min_i;
  while (*opt) {
    const char* sep = strchr(opt, '|');
    size_t olen = sep ? (size_t)(sep - opt) : strlen(opt);
    // blank holds a number this build hasn't got
    if (olen) {
      int n = snprintf_P(c.buf, sizeof(c.buf),
                         PSTR("<option value=%ld%s>%.*s%s</option>"),
                         idx, (idx == cur_idx) ? PSTR(" selected") : PSTR(""),
                         (int)olen, opt, (idx == def_idx) ? PSTR(" (default)") : PSTR(""));
      if (n > 0 && (size_t)n < sizeof(c.buf)) c.res->sendChunk(c.buf, (size_t)n);
    }
    if (!sep) break;
    opt = sep + 1;
    idx++;
  }
}

static void param_emit_enum(ParamCtx& c, const EGPref& p) {
  if (c.pos) { c.res->sendChunk(c.buf, c.pos); c.pos = 0; }
  int n = snprintf_P(c.buf, sizeof(c.buf),
                     PSTR("<select id='%s.%s' name='%s.%s'%s>"),
                     c.mid, c.s_id, c.mid, c.s_id, c.disabled);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.res->sendChunk(c.buf, (size_t)n);
  param_emit_options(c, p);
  n = snprintf_P(c.buf, sizeof(c.buf), PSTR("</select><br>"));
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.res->sendChunk(c.buf, (size_t)n);
  c.pos = 0;
}

static PGM_P param_input_type(const EGPref& p, bool slider) {
  if (slider) return PSTR("range");
  if (p.type == EGP_INT || p.type == EGP_UINT || p.type == EGP_FLOAT) return PSTR("number");
  if (p.type == EGP_STRING && (p.flags & EGP_SENSITIVE)) return PSTR("password");
  if (p.type == EGP_STRING && (p.flags & EGP_TIME))      return PSTR("time");
  return PSTR("text");
}

// html grows 6x, a cut value would be saved cut
static constexpr size_t ESC_PIECE = sizeof(ParamCtx::buf) / 6;

static void param_send_escaped(ParamCtx& c, const char* s) {
  char piece[ESC_PIECE + 1];
  for (size_t left = strlen(s); left; ) {
    size_t k = left < ESC_PIECE ? left : ESC_PIECE;
    memcpy(piece, s, k);
    piece[k] = '\0';
    c.res->sendChunk(c.buf, egesc_html(c.buf, sizeof(c.buf), piece));
    s += k;
    left -= k;
  }
}

static void param_emit_value(ParamCtx& c, const EGPref& p, bool slider) {
  if (p.flags & EGP_SENSITIVE) {
    PGM_P ph = c.cur[0] ? PSTR("(unchanged \xE2\x80\x94 leave blank)") : PSTR("(not set)");
    param_appendf(c, PSTR(" value='' placeholder='%s'"), ph);
    return;
  }
  if (c.pos) { c.res->sendChunk(c.buf, c.pos); c.pos = 0; }
  c.res->sendChunk(F(" value='"));
  param_send_escaped(c, c.cur);
  c.buf[c.pos++] = '\'';
  if (!slider && p.default_val && p.default_val[0]
      && !(p.flags & EGP_TIME)) {
    param_appendf(c, PSTR(" placeholder='%s'"), p.default_val);
  }
}

static void param_emit_limits(ParamCtx& c, const EGPref& p) {
  if (p.type == EGP_STRING && p.max_len > 0) {
    param_appendf(c, PSTR(" maxlength='%u'"), (unsigned)p.max_len);
  }
  if (p.type == EGP_STRING && p.pattern && c.s_pat[0]) {
    param_appendf(c, PSTR(" pattern='%s'"), c.s_pat);
  }
  if ((p.type == EGP_INT || p.type == EGP_UINT || p.type == EGP_FLOAT) && p.min_i != p.max_i) {
    param_appendf(c, PSTR(" min=%ld max=%ld"), (long)p.min_i, (long)p.max_i);
  }
}

static void param_emit_input_end(ParamCtx& c, const EGPref& p, bool slider) {
  if (p.type == EGP_FLOAT) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " step=any");
  if (!slider && (p.type == EGP_INT || p.type == EGP_UINT)) {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " inputmode=numeric");
  } else if (p.type == EGP_FLOAT) {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " inputmode=decimal");
  }
  if (p.flags & EGP_REQUIRED) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " required");
  if (p.flags & EGP_READONLY) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " readonly");
  if (slider) {
    param_appendf(c,
      PSTR(" oninput='this.nextElementSibling.value=this.value'><output>%s</output><br>"), c.cur);
  } else {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), "><br>");
  }
}

static void param_emit_input(ParamCtx& c, const EGPref& p) {
  const bool slider = (p.type == EGP_INT || p.type == EGP_UINT)
                   && (p.flags & EGP_SLIDER)
                   && p.min_i != p.max_i;
  param_appendf(c, PSTR("<input type=%s id='%s.%s' name='%s.%s'"),
                param_input_type(p, slider), c.mid, c.s_id, c.mid, c.s_id);
  param_emit_value(c, p, slider);
  param_emit_limits(c, p);
  param_emit_input_end(c, p, slider);
}

static void param_emit_help(ParamCtx& c, const EGPref& p, bool inline_bool) {
  if (!inline_bool && p.help && c.s_help[0]) {
    param_appendf(c, PSTR("<small>%s</small>"), c.s_help);
  }
  if ((p.flags & EGP_SENSITIVE) && c.cur[0]) {
    param_appendf(c,
      PSTR("<button type=button class='danger btn-sm' style='margin-top:.2em' "
           "onclick=\"if(confirm('Clear stored value?')){byID('%s.%s').value='__CLEAR__';this.form.submit();}\""
           ">Clear</button>"),
      c.mid, c.s_id);
  }
  c.res->sendChunk(c.buf, c.pos);
}

static void param_load_text(ParamCtx& c, const EGPref& p) {
  flash_to_ram(p.id,      c.s_id,   sizeof(c.s_id));
  flash_to_ram(p.label,   c.s_lbl,  sizeof(c.s_lbl));
  flash_to_ram(p.help,    c.s_help, sizeof(c.s_help));
  flash_to_ram(p.pattern, c.s_pat,  sizeof(c.s_pat));
  if (strcmp_P(c.s_id, EG_K_LAT) == 0) c.has_lat = true;
  else if (strcmp_P(c.s_id, EG_K_LON) == 0) c.has_lon = true;
}

static void param_emit_heading(ParamCtx& c, const EGPref& p) {
  int n = snprintf_P(c.buf, sizeof(c.buf),
                     p.type == EGP_LABEL ? PSTR("<label>%s</label>") : PSTR("<h4>%s</h4>"),
                     c.s_lbl);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.res->sendChunk(c.buf, (size_t)n);
}

static void param_emit_label(ParamCtx& c, const char* lbl) {
  int n = snprintf_P(c.buf, sizeof(c.buf), PSTR("<label for='%s.%s'>%s</label>"), c.mid, c.s_id, lbl);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.pos = (size_t)n;
}

static void param_emit_row(ParamCtx& c, const EGPref& p);

// a row with a show rule sits in its own div
static void param_emit_field(ParamCtx& c, const EGPref& p) {
  param_load_text(c, p);
  bool wrap = show_attr(c, c.s_id)[0];
  if (wrap) {
    c.res->sendChunk(F("<div"));
    c.res->sendChunk(c.s_show);
    c.res->sendChunk(F(">"));
  }
  param_emit_row(c, p);
  if (wrap) c.res->sendChunk(F("</div>"));
}

static void param_emit_row(ParamCtx& c, const EGPref& p) {
  if (p.type == EGP_LABEL || p.type == EGP_HEADER) {
    param_emit_heading(c, p);
    return;
  }

  c.cur = EGPrefs::getString(c.mid, c.s_id);
  const char* lbl = p.label ? c.s_lbl : c.s_id;
  c.disabled = (p.flags & EGP_READONLY) ? PSTR(" disabled") : PSTR("");
  bool inline_bool = (p.type == EGP_BOOL) && (p.flags & EGP_INLINE);
  c.pos = 0;
  if (!inline_bool) param_emit_label(c, lbl);

  if (p.type == EGP_BOOL)      param_emit_bool(c, lbl, inline_bool);
  else if (p.type == EGP_ENUM) param_emit_enum(c, p);
  else                         param_emit_input(c, p);
  param_emit_help(c, p, inline_bool);
}

static void param_render_group(ParamCtx& c, const EGPrefGroup* g, EGModule* mod) {
  param_group_header(c, g, mod);
  c.mid = g->module_id;
  c.has_lat = false;
  c.has_lon = false;

  for (size_t j = 0; j < g->count.get(); j++) {
    const EGPref p = g->prefs.row(j);
    if (p.flags & (EGP_HIDDEN | EGP_ADVANCED)) continue;
    param_emit_field(c, p);
  }
  if (c.has_lat && c.has_lon) param_emit_locator(c);
  bool adv_open = false;
  for (size_t j = 0; j < g->count.get(); j++) {
    const EGPref p = g->prefs.row(j);
    if ((p.flags & EGP_HIDDEN) || !(p.flags & EGP_ADVANCED)) continue;
    if (!adv_open) {
      c.res->sendChunk(F("<details class=adv><summary>Advanced</summary>"));
      adv_open = true;
    }
    param_emit_field(c, p);
  }
  if (adv_open) c.res->sendChunk(FPSTR(W_DETAILS_END));
  c.res->sendChunk(FPSTR(W_DETAILS_END));
}

static void param_render_groups(EGHttpResponse& res, uint8_t tab) {
  ParamCtx c;
  c.res = &res;
  uint8_t gc = (uint8_t)EGPrefs::group_count();
  uint8_t order[EG_MAX_MODULES];
  if (gc > sizeof(order)) gc = sizeof(order);
  param_sort_groups(order, gc);

  for (uint8_t gi = 0; gi < gc; gi++) {
    EGModule* mod = EGPrefs::module_at(order[gi]);
    if (mod && mod->display_order() == 0) continue;   // has its own page
    const EGPrefGroup* g = EGPrefs::group_at(order[gi]);
    if (!g || g->count.get() == 0) continue;
    if (g->category.get() != tab) continue;
    c.show = mod ? mod->prefs_show() : nullptr;
    param_render_group(c, g, mod);
  }
}

static void param_render_form(EGHttpResponse& res, uint8_t tab) {
  char form_open[48];
  int fo = snprintf_P(form_open, sizeof(form_open),
    PSTR("<form method=POST action='/param?t=%u'>"), (unsigned)tab);
  if (fo > 0 && (size_t)fo < sizeof(form_open)) res.sendChunk(form_open, (size_t)fo);
  param_render_groups(res, tab);
  res.sendChunk(F(
    "<hr><div id=fErr class='a ae' style=display:none>"
    "Some fields need fixing - check expanded sections.</div>"
    "<button type=submit>Save</button>"
    "<script>document.forms[0].addEventListener('invalid',e=>{"
    "for(let n=e.target;n;n=n.parentNode)n.tagName=='DETAILS'&&(n.open=1);"
    "fErr.style.display=''},true);"
    "function sw(){document.querySelectorAll('[data-w]').forEach(e=>{var c=byID(e.dataset.w);"
    "if(c)e.hidden=!(e.dataset.m>>(c.type=='checkbox'?+c.checked:+c.value)&1)})}"
    "addEventListener('change',sw);sw()</script></form>"));
}

static uint8_t param_tab(EGHttpRequest& req) {
  int32_t t;
  if (parse_i32(req.arg(F("t")), &t) && t >= 0 && t <= 4) return (uint8_t)t;
  return (uint8_t)EGP_CAT_SYSTEM;
}

void WebPortal::hParam(EGHttpRequest& req, EGHttpResponse& res, void*) {
  uint8_t tab = param_tab(req);
  bool posted = (req.method() == EGHttpRequest::POST);
  Refusal refused{};
  bool saved = posted && param_save_body(tab, refused);
  if (saved) {
    EGPrefs::commit();
    // stay, so the refusal shows
    if (EGPrefs::restart_pending() && !refused.why) {
      res.redirect(FPSTR(P_RESTART));
      return;
    }
  }

  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Config"));
  if (saved) res.sendChunk(F(
    "<div class='a ak'>Saved.</div>"));
  if (refused.why) {
    char buf[160];
    snprintf_P(buf, sizeof(buf), PSTR("<div class='a ae'>Not saved: %s, %s.%s</div>"),
               refused.name, refused.why,
               EGPrefs::restart_pending() ? PSTR(" <a href=/restart>Restart</a> for the rest.") : PSTR(""));
    res.sendChunk(buf);
  }
  if (posted && !saved) res.sendChunk(F(
    "<div class='a ae'>Not saved: the form was too large or memory was short.</div>"));
  param_render_tabs(res, tab);

  // Backup is the import form only
  if (tab == (uint8_t)EGP_CAT_BACKUP) param_render_backup(res);
  else                                param_render_form(res, tab);
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      uint32_t mask = -(int32_t)(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320 & mask);
    }
  }
  return ~crc;
}

static bool is_skip_pref(const char* module_id, const char* key) {
  if (strcmp_P(module_id, PSTR("sys")) == 0)
    return strcmp_P(key, EG_K_WEB_PASS) == 0 || strcmp_P(key, PSTR("cpu_mhz")) == 0;
  if (strcmp_P(module_id, PSTR("net")) == 0) return true;
  if (strcmp_P(module_id, PSTR("wifi")) == 0) return true;
  return false;
}

static constexpr uint32_t EXPORT_OBF_SEED = 0xB11C5EEDu;

struct B64Sink {
  EGHttpResponse* res;
  uint8_t  tri[3];
  uint8_t  triLen;
  char     out[256];
  size_t   outLen;
  uint32_t crc;
  uint32_t obf_state;
  bool     obf_on;
};

static void b64_init(B64Sink& s, EGHttpResponse& r) {
  s.res = &r; s.triLen = 0; s.outLen = 0; s.crc = 0;
  s.obf_state = EXPORT_OBF_SEED; s.obf_on = false;
}

static inline uint8_t obf_step(uint32_t* st) {
  uint32_t x = *st;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  *st = x;
  return (uint8_t)(x & 0xFF);
}
static void b64_flush_out(B64Sink& s) {
  if (s.outLen) { s.res->sendChunk(s.out, s.outLen); s.outLen = 0; }
}
static void b64_emit_quartet(B64Sink& s, uint8_t in_len) {
  if (s.outLen + 4 > sizeof(s.out)) b64_flush_out(s);
  // writes a NUL too
  unsigned char quad[5];
  encode_base64(s.tri, in_len, quad);
  memcpy(s.out + s.outLen, quad, 4);
  s.outLen += 4;
  s.triLen = 0;
}
static void b64_write(B64Sink& s, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = p[i];
    if (s.obf_on) b ^= obf_step(&s.obf_state);
    s.crc = crc32_update(s.crc, &b, 1);
    s.tri[s.triLen++] = b;
    if (s.triLen == 3) b64_emit_quartet(s, 3);
  }
}
static void b64_write_u8(B64Sink& s, uint8_t v) { b64_write(s, &v, 1); }
static void b64_finish(B64Sink& s) {
  if (s.triLen > 0) b64_emit_quartet(s, s.triLen);
  b64_flush_out(s);
}

static bool is_exportable_pref(const EGPrefGroup* g, const EGPref& p,
                               const char* id_buf, const char*& val_out) {
  if (p.type == EGP_LABEL || p.type == EGP_HEADER) return false;
  if (is_skip_pref(g->module_id, id_buf)) return false;
  const char* cur = EGPrefs::getString(g->module_id, id_buf);
  if (!cur) cur = "";
  if (p.default_val && strcmp(cur, p.default_val) == 0) return false;
  val_out = cur;
  return true;
}

static bool group_has_export(const EGPrefGroup* g) {
  for (size_t j = 0; j < g->count.get(); j++) {
    char id_buf[EGPREF_ID_MAX + 1];
    strncpy_P(id_buf, g->prefs.id_P(j), sizeof(id_buf) - 1);
    id_buf[sizeof(id_buf) - 1] = '\0';
    const char* cur;
    if (is_exportable_pref(g, g->prefs.row(j), id_buf, cur)) return true;
  }
  return false;
}

static uint8_t group_export_count(const EGPrefGroup* g) {
  uint8_t emitted_prefs = 0;
  for (size_t j = 0; j < g->count.get(); j++) {
    char id_buf[EGPREF_ID_MAX + 1];
    strncpy_P(id_buf, g->prefs.id_P(j), sizeof(id_buf) - 1);
    id_buf[sizeof(id_buf) - 1] = '\0';
    const char* cur;
    if (is_exportable_pref(g, g->prefs.row(j), id_buf, cur)) emitted_prefs++;
  }
  return emitted_prefs;
}

static void export_write_group(B64Sink& sink, const EGPrefGroup* g, uint8_t emitted_prefs) {
  size_t mid_len = strlen(g->module_id);
  if (mid_len > 255) return;
  b64_write_u8(sink, (uint8_t)mid_len);
  b64_write(sink, g->module_id, mid_len);
  b64_write_u8(sink, emitted_prefs);

  for (size_t j = 0; j < g->count.get(); j++) {
    const EGPref p = g->prefs.row(j);
    char id_buf[EGPREF_ID_MAX + 1];
    strncpy_P(id_buf, p.id, sizeof(id_buf) - 1);
    id_buf[sizeof(id_buf) - 1] = '\0';
    const char* cur;
    if (!is_exportable_pref(g, p, id_buf, cur)) continue;
    size_t klen = strlen(id_buf);
    size_t vlen = strlen(cur);
    if (klen > 255 || vlen > 65535) continue;
    b64_write_u8(sink, (uint8_t)klen);
    b64_write(sink, id_buf, klen);
    b64_write_u8(sink, (uint8_t)((vlen >> 8) & 0xFF));
    b64_write_u8(sink, (uint8_t)(vlen & 0xFF));
    b64_write(sink, cur, vlen);
  }
}

static void serializeExportStream(B64Sink& sink) {
  uint8_t group_count = 0;
  for (size_t gi = 0; gi < EGPrefs::group_count(); gi++) {
    const EGPrefGroup* g = EGPrefs::group_at(gi);
    if (!g || g->count.get() == 0) continue;
    if (group_has_export(g)) group_count++;
  }

  const uint8_t MAGIC[3] = {0x11, 0x23, 0xC6};
  b64_write(sink, MAGIC, 3);
  b64_write_u8(sink, 1);
  sink.obf_on = true;
  b64_write_u8(sink, group_count);

  for (size_t gi = 0; gi < EGPrefs::group_count(); gi++) {
    const EGPrefGroup* g = EGPrefs::group_at(gi);
    if (!g || g->count.get() == 0) continue;
    uint8_t emitted_prefs = group_export_count(g);
    if (emitted_prefs == 0) continue;
    export_write_group(sink, g, emitted_prefs);
  }
}

static void deobfuscate_payload(uint8_t* buf, size_t len) {
  uint32_t st = EXPORT_OBF_SEED;
  for (size_t i = 0; i < len; i++) buf[i] ^= obf_step(&st);
}

static const char* validateImport(uint8_t* buf, size_t len) {
  if (len < 10) return PSTR("too short");
  if (buf[0] != 0x11 || buf[1] != 0x23 || buf[2] != 0xC6) return PSTR("bad magic");
  if (buf[3] != 1) return PSTR("bad format");
  size_t payload_len = len - 4;
  uint32_t expected = ((uint32_t)buf[payload_len    ] << 24) |
                      ((uint32_t)buf[payload_len + 1] << 16) |
                      ((uint32_t)buf[payload_len + 2] <<  8) |
                      ((uint32_t)buf[payload_len + 3]      );
  uint32_t actual = crc32_update(0, buf, payload_len) ^ EXPORT_OBF_SEED;
  if (expected != actual) return PSTR("checksum mismatch");

  deobfuscate_payload(buf + 4, payload_len - 4);
  return ImportText::blob_error(buf + 4, payload_len - 4);
}

// put refuses unknown keys, forwards moved ones
static bool import_put(const char* mid, const char* key, const char* value) {
  if (is_skip_pref(mid, key)) return false;
  return EGPrefs::put(mid, key, value);
}

// wipe then apply, -1 = bad blob
static int applyImport(uint8_t* buf, size_t len, const char** err_out) {
  const char* err = validateImport(buf, len);
  if (err) { if (err_out) *err_out = err; return -1; }

  EGPrefs::reset_all(/*keep_network=*/true);
  int applied = ImportText::apply_blob(buf + 4, len - 8, import_put);   // less magic and crc
  EGPrefs::commit(false);   // silent, the cascade would WDT
  return applied;
}

void WebPortal::hExport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  res.beginChunked(200, FPSTR(HTTP_CT_PLAIN));
  B64Sink sink;
  b64_init(sink, res);
  serializeExportStream(sink);
  uint32_t crc = sink.crc ^ EXPORT_OBF_SEED;
  uint8_t crc_be[4] = {
    (uint8_t)((crc >> 24) & 0xFF),
    (uint8_t)((crc >> 16) & 0xFF),
    (uint8_t)((crc >> 8)  & 0xFF),
    (uint8_t)( crc        & 0xFF),
  };
  // not b64_write, the CRC isn't hashed
  for (int i = 0; i < 4; i++) {
    sink.tri[sink.triLen++] = crc_be[i];
    if (sink.triLen == 3) b64_emit_quartet(sink, 3);
  }
  b64_finish(sink);
  res.endChunked();
}

static BodyBuf s_importBody;

static void import_body_begin(size_t cap) {
  body_free(s_importBody);
  if (body_size_ok(cap)) (void)body_alloc(s_importBody, cap);
}

// too big or no memory, hImport says no body
void WebPortal::hImportBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* chunk, size_t len, void*) {
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: import_body_begin(req.bodyLen());         break;
    case EGHttpServer::BODY_DATA:  body_append(s_importBody, chunk, len);    break;
    case EGHttpServer::BODY_END:   body_end(s_importBody);                   break;
    case EGHttpServer::BODY_ABORT: body_free(s_importBody);                  break;
  }
}

static int hex_nyb(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static char* find_blob_value(char* body, size_t bodyLen) {
  for (size_t i = 0; i + 5 <= bodyLen; i++) {
    bool at_start = (i == 0 || body[i - 1] == '&');
    if (at_start && memcmp_P(body + i, PSTR("blob="), 5) == 0) return body + i + 5;
  }
  return nullptr;
}

static char* url_decode_in_place(char* src, char* end) {
  char* dst = src;
  while (src < end && *src != '&') {
    if (*src == '+') { *dst++ = ' '; src++; }
    else if (*src == '%' && src + 2 < end) {
      int hi = hex_nyb(src[1]);
      int lo = hex_nyb(src[2]);
      if (hi >= 0 && lo >= 0) { *dst++ = (char)((hi << 4) | lo); src += 3; }
      else                    { *dst++ = *src++; }
    }
    else { *dst++ = *src++; }
  }
  return dst;
}

// decodes in place
static char* extract_blob_field(char* body, size_t bodyLen, size_t* out_len) {
  if (bodyLen < 5) return nullptr;
  char* eq = find_blob_value(body, bodyLen);
  if (!eq) return nullptr;
  char* dst = url_decode_in_place(eq, body + bodyLen);
  *dst = '\0';
  if (out_len) *out_len = dst - eq;
  return eq;
}

static void import_fail(EGHttpResponse& res, const char* msg) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Import"));
  char buf[160];
  snprintf_P(buf, sizeof(buf),
             PSTR("<p>Import failed: %s.</p><p><a href=/param>Back</a></p>"), msg);
  res.sendChunk(buf);
  WebPortal::sendPageTail(res);
  res.endChunked();
  body_free(s_importBody);
}

// decoded in the body buffer
static char* import_field(size_t* len, const char** err) {
  if (!s_importBody.ok || !s_importBody.buf || s_importBody.len == 0) {
    *err = PSTR("no body");
    return nullptr;
  }
  char* field = extract_blob_field(s_importBody.buf, s_importBody.len, len);
  if (!field || *len == 0) {
    *err = PSTR("missing blob");
    return nullptr;
  }
  return field;
}

// malloc'd blob, or nullptr and err
static uint8_t* import_decode(char* b64, size_t b64_len, unsigned int* raw_len, const char** err) {
  unsigned int raw_cap = decode_base64_length((unsigned char*)b64, b64_len);
  if (raw_cap == 0 || raw_cap > 8192) {
    *err = PSTR("bad base64");
    return nullptr;
  }
  uint8_t* raw = (uint8_t*)malloc(raw_cap);
  if (!raw) {
    *err = PSTR("alloc failed");
    return nullptr;
  }
  *raw_len = decode_base64((unsigned char*)b64, b64_len, raw);
  return raw;
}

// wipe then apply, -1 = bad
static int import_blob(char* b64, size_t b64_len, const char** err) {
  unsigned int raw_len = 0;
  uint8_t* raw = import_decode(b64, b64_len, &raw_len, err);
  body_free(s_importBody);
  if (!raw) return -1;
  int applied = applyImport(raw, raw_len, err);
  free(raw);
  return applied;
}

// only the listed prefs change, each one validated; -1 = none
static int import_lines(const char* text, size_t len, ImportText::Result* out, const char** err) {
  ImportText::Result r = ImportText::apply(text, len, import_put);
  body_free(s_importBody);
  *out = r;
  if (r.applied == 0) {
    *err = PSTR("nothing imported");
    return -1;
  }
  EGPrefs::commit(false);   // silent, the cascade would WDT
  return r.applied;
}

void WebPortal::hImport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  const char* err = nullptr;
  size_t len = 0;
  ImportText::Result r = {0, 0, 0};
  int applied = -1;
  char* field = import_field(&len, &err);
  if (field && ImportText::is_blob(field, len)) applied = import_blob(field, len, &err);
  else if (field) applied = import_lines(field, len, &r, &err);
  if (applied < 0) {
    import_fail(res, err ? err : PSTR("bad blob"));
    return;
  }
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Imported"));
  char msg[112];
  if (r.skipped) {
    snprintf_P(msg, sizeof(msg), PSTR("<p>Imported %d prefs, skipped %d (first at line %d). Restarting.</p>"),
               applied, r.skipped, r.first_skip);
  } else {
    snprintf_P(msg, sizeof(msg), PSTR("<p>Imported %d prefs. Restarting.</p>"), applied);
  }
  res.sendChunk(msg);
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

void WebPortal::hOutputs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  char buf[512];
  size_t pos = 0;
  buf[pos++] = '{';
  pos += EGModuleRegistry::collect_status_json(buf + pos, sizeof(buf) - pos - 2, fast_millis());
  buf[pos++] = '}';
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(buf, pos);
  res.endChunked();
}

void WebPortal::hVars(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(F("{"));
  bool first = true;
  for (EGMeasureIter it(EGM_OUT_VARS); it.next(); ) {
    const EGMeasure& v = it.row();
    char raw[64];
    if (egm_format_display(v, it.value(), raw, sizeof(raw)) == 0) continue;
    bool str = v.kind == EGM_STR;
    const char* shown = raw;
    char esc[6 * (sizeof(raw) - 1) + 1];   // escaped worst case
    if (str) {
      egesc_json(esc, sizeof(esc), raw);
      shown = esc;
    }
    char head[40];
    int m = snprintf_P(head, sizeof(head),
                       str ? PSTR("%s\"%s\":\"") : PSTR("%s\"%s\":"),
                       first ? PSTR("") : PSTR(","), v.id);
    if (m <= 0 || (size_t)m >= sizeof(head)) continue;
    res.sendChunk(head, (size_t)m);
    res.sendChunk(shown, strlen(shown));
    if (str) res.sendChunk(F("\""));
    first = false;
  }
  res.sendChunk(F("}"));
  res.endChunked();
}

void WebPortal::metricsLine(EGHttpResponse& res, PGM_P fmt, ...) {
  char buf[320];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf_P(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);
}

static void metrics_render_info(EGHttpResponse& res, const char* cid, const char* name) {
  res.sendChunk(F("# HELP device_info Device metadata; value always 1. Join via on(chipid) for naming.\n"
                  "# TYPE device_info gauge\n"));
  char esc_name[3 * EG_NAME_MAX + 1];
  egesc_prom(esc_name, sizeof(esc_name), name);
  WebPortal::metricsLine(res,
    PSTR("device_info{chipid=\"%s\",name=\"%s\",model=\"" APP_MODEL "\",ver=\"" RELEASE_VERSION "\",env=\"" BUILD_ENV "\"} 1\n"),
    cid, esc_name);
}

// temperature in Celsius
static void metrics_render_rows(EGHttpResponse& res, const char* cid) {
  char buf[320];
  const char* last = nullptr;
  for (EGMeasureIter it(EGM_OUT_PROM); it.next(); ) {
    const EGMeasure& m = it.row();
    size_t n = egm_prom_format(m, it.value(), cid, m.prom != last, buf, sizeof(buf));
    if (n == 0) continue;
    res.sendChunk(buf, n);
    last = m.prom;
  }
}

void WebPortal::hMetrics(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // chipid only, a rename keeps the series
  const char* cid  = DeviceInfo::chipid();
  const char* fn   = DeviceInfo::friendlyName();
  const char* name = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  res.beginChunked(200, F("text/plain; version=0.0.4"));
  metrics_render_info(res, cid, name);
  metrics_render_rows(res, cid);
  AppHooks::metrics(res, cid);
  res.endChunked();
}

void WebPortal::hWebCmd(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* body = req.body();
  if (!body || !*body) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("empty command"));
    return;
  }
  AppHooks::console(body);
  res.send(200, FPSTR(HTTP_CT_PLAIN), F("ok"));
}

void WebPortal::hConsoleStream(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // "<idx>,<tz_min>\n" then new lines
  int32_t c1 = 0;
  parse_i32(req.arg(F("c1")), &c1);
  uint32_t counter = (uint32_t)c1;

  res.addHeader(FPSTR(HTTP_H_CACHE), F("no-store"));
  res.beginChunked(200, FPSTR(HTTP_CT_PLAIN));

  // read once, sendChunk can yield
  uint8_t curIdx = (uint8_t)Log::getLogIdx();
  char hdr[24];
  int n = snprintf_P(hdr, sizeof(hdr), PSTR("%u,%d\n"),
                     (unsigned)curIdx, Clock::tz_offset_min);
  if (n > 0 && (size_t)n < sizeof(hdr)) res.sendChunk(hdr, (size_t)n);

  if (counter != curIdx) {
    if (counter == 0) counter = curIdx;
    do {
      char line[256];
      size_t copyLen = Log::copyLog(counter, line, sizeof(line));
      if (copyLen) {
        line[copyLen - 1] = '\n';
        line[copyLen] = '\0';
        res.sendChunk(line, copyLen);
      }
      counter++;
      counter &= 0xFF;
      if (counter == 0) counter++;  // 0 reserved
    } while ((uint8_t)counter != curIdx);
  }
  res.endChunked();
}

extern const char wifiAdvancedHTML[] PROGMEM = R"HTML(
<details style='margin-top:1.5em'><summary>Advanced</summary>
<p class=muted>Erase the saved WiFi credentials and reboot into the captive setup portal.</p>
<form method=POST action=/erase onsubmit="return confirm('Erase saved WiFi credentials? Device will reboot into setup mode.')">
<button type=submit class=danger>Erase WiFi config</button>
</form>
<p class=muted style=margin-top:1em>Factory reset: erase <b>all</b> settings and calibration, then reboot into the setup portal.</p>
<form method=POST action=/reset onsubmit="return confirm('Erase ALL settings and reboot into setup mode? This cannot be undone.')">
<button type=submit class=danger>Factory reset</button>
</form>
</details>
<script>
(function(){
  var sel=byID('ws');
  var rb=byID('rescan');
  var pollT=null;
  function esc(s){return s.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
  function poll(force){
    rb.disabled=true;
    fetch('/wifiscan'+(force?'?refresh=1':''))
      .then(r=>r.json())
      .then(d=>{
        if(d.state==='scanning'){
          sel.innerHTML='<option value="">Scanning&hellip;</option>';
          pollT=setTimeout(function(){poll(false);},1500);
        }else if(d.state==='complete'){
          rb.disabled=false;
          var opts='<option value="">-- pick a network --</option>';
          (d.aps||[]).forEach(function(a){
            var lock=a.open?'':' \u{1F512}';
            opts+='<option data-ssid="'+esc(a.ssid)+'">'+esc(a.ssid)+' ('+a.rssi+' dBm)'+lock+'</option>';
          });
          sel.innerHTML=opts;
        }else{
          rb.disabled=false;
          sel.innerHTML='<option value="">(scan failed)</option>';
        }
      })
      .catch(function(){rb.disabled=false;sel.innerHTML='<option value="">(error)</option>';});
  }
  rb.addEventListener('click',function(e){e.preventDefault();clearTimeout(pollT);poll(true);});
  poll(false);
})();
</script>
)HTML";

// picograph.js - https://github.com/RainingComputers/picograph.js
// minimised, with adjustments to allow fixed scroll
#if !EG_GZ_JS_BUNDLE || !EG_GZ_HVJS_BUNDLE
extern const char picographJS[] PROGMEM = R"JS(
var cVID=(t,s)=>t.map(x=>s+x.replace(" ","")+"value"),
gTS=()=>new Date().toLocaleTimeString(),
sclInv=(t,s,i,e)=>(1-(t-s)/(i-s))*e,
sAL=(t,s)=>(t.shift(),t.push(s),t),
eA2D=(t,s,i)=>Array(t).fill().map(_=>Array(s).fill(i)),
eA=(t,s)=>Array(t).fill(s),
cls=["#D35","#090","#00F","#F0F","#933","#009","#099","#999"],
cA=t=>Array(t).fill().map((_,i)=>cls[i%cls.length]);
function cLeg(t,s,i,e){const h=`<span>${i}</span>`,n=":"==i.at(-1)?`<span id="${e}"></span>`:"";byID(t).innerHTML+=`<div style=display:inline-block><svg width=10 height=10><rect width=10 height=10 fill="${s}"/></svg> ${h} ${n}</div>`}
class Graph{
constructor(t,s,i,e,h,n,a=0,l=!1,o=!1,r=5,c=1,m=2){const d=cVID(s,t);this.iE(t,e,d),this.sC(s,i,n,a,l,o,r,c,m),this.tc=0,this.vc=0,this.rs(h),""===byID(e).innerHTML&&this.cL(),addEventListener("themechange",()=>{this.fg=null;this.rd()})}
iE(t,s,i){this.cv=byID(t),this.ctx=this.cv.getContext("2d"),this.sW(),this.lID=s,this.vIDs=i}
sC(t,s,i,e,h,n,a,l,o){this.lb=t,this.u=s,this.vF=l,this.vl=h,this.tm=n,this.nL=t.length,this.co=cA(this.nL);var d=document.createElement("textarea");d.innerHTML=s;this.uD=d.value;this.scC(i,e,a,o)}
scC(t,s,i,e){this.mv=t,this.lv=s,this.ss=i,this.aS=e,e!=1&&e!=2&&(this.aS=0,Number.isFinite(s)||(this.lv=0),Number.isFinite(t)||(this.mv=100))}
rs(t){this.iS=t*this.S,this.nF=this.w/this.iS,this.nP=Math.round(this.nF)+1,this.P=eA2D(this.nL,this.nP),this.tsA=eA(this.nP,""),this.fz=null,this.hs=null,this.sp=null}
cL(){byID(this.lID).innerHTML="";for(let t=0;t<this.nL;t++)cLeg(this.lID,this.co[t],this.lb[t]+":",this.vIDs[t])}
uCfg(t,s,i,e,h=0,n=!1,a=!1,l=5,o=1,r=1){this.cl(),this.sC(t,s,e,h,n,a,l,o,r),this.rs(i),this.cL()}
uMM(t){t<this.lv&&(this.lv=t),t>this.mv&&(this.mv=t);let s=Math.abs(this.mv-this.lv),mn=Infinity,mx=-Infinity;for(const r of this.P)for(const v of r)Number.isFinite(v)&&(v<mn&&(mn=v),v>mx&&(mx=v));const i=Math.ceil(mx+.05*s),e=Math.floor(mn)-.05*s;if(2==this.aS)return this.mv=i;t>this.mv-.05*s&&(this.mv=i),t<this.lv+.05*s&&(this.lv=e)}
uP(t){for(let s=0;s<this.nL;s++)this.aS>0&&this.uMM(t[s]),this.P[s]=sAL(this.P[s],t[s])}
uL(t){for(let s=0;s<this.nL;s++)byID(this.vIDs[s]).innerHTML=t[s].toFixed(2)+" "+this.u}
uT(ts){this.tsA=sAL(this.tsA,ts||gTS())}
dV(){this.vc++;const c=this.ctx,h=this.h;c.strokeStyle=this.gC;c.beginPath();for(let t=this.nP-this.vc;t>=0;t-=this.vF){const s=t*this.iS;c.moveTo(s,0);c.lineTo(s,h)}c.stroke();this.vc==this.vF&&(this.vc=0)}
cl(){this.ctx.clearRect(0,0,this.w,this.h)}
sW(){this.S=window.devicePixelRatio,this.cv.width=this.cv.clientWidth*this.S,this.cv.height=this.cv.clientHeight*this.S,this.w=this.ctx.canvas.width,this.h=this.ctx.canvas.height}
sIW(){this.iS=this.w/this.nF,this.ctx.lineWidth=2*this.S}
sS(){this.hs=this.h/this.ss,this.sp=(this.mv-this.lv)/this.ss,this.fz=Math.min(.5*this.hs,15*this.S),this.ctx.font=this.fz+"px monospace";if(!this.fg){const cs=getComputedStyle(this.cv);this.fg=cs.color;this.gC=cs.borderTopColor}this.ctx.fillStyle=this.fg}
dH(){const c=this.ctx,w=this.w,h=this.h,fs=this.fz+2*this.S,u=this.uD;c.strokeStyle=this.gC;c.beginPath();for(let s=1;s<=this.ss;s++){const i=h-s*this.hs;c.fillText((s*this.sp+this.lv).toFixed(2)+" "+u,2,i+fs);c.moveTo(0,i);c.lineTo(w,i)}c.stroke()}
dT(){const c=this.ctx,h=this.h,fs=this.fz+2*this.S,t=c.measureText((this.ss*this.sp).toFixed(2)).width,s=Math.floor(t/this.iS+1)+1;this.tc++;for(let t=this.nP-this.tc;t>=s;t-=this.vF){const s=t*this.iS,e=c.measureText(this.tsA[t]).width+4*this.S;c.rotate(Math.PI/2),c.fillText(this.tsA[t],h-e,-s+fs),c.stroke(),c.rotate(-Math.PI/2)}this.tc==this.vF&&(this.tc=0)}
dr(){const c=this.ctx,lv=this.lv,mv=this.mv,h=this.h,sz=this.iS,F=Number.isFinite;for(let t=this.nL-1;t>=0;t--){c.strokeStyle=this.co[t];c.beginPath();let st=0;for(let s=this.nP-1;s>=0;s--){const v=this.P[t][s];if(!F(v)){st=0;continue}const y=sclInv(v,lv,mv,h),x=s*sz;if(st)c.lineTo(x,y);else c.moveTo(x,y);st=1}c.stroke()}}
rd(){this.cl(),this.sW(),this.sIW(),this.sS(),this.vl&&this.dV(),this.dH(),this.tm&&this.dT(),this.dr()}
update(t,ts){this.uP(this.sm(t)),this.uL(t),this.uT(ts),this.rd()}
sm(t){if(!(this.av>1))return t;this.hb=this.hb||[];return t.map((v,i)=>{const h=this.hb[i]=this.hb[i]||[];h.push(v),h.length>this.av&&h.shift();return h.reduce((a,b)=>a+b,0)/h.length})}
})JS";
#endif
