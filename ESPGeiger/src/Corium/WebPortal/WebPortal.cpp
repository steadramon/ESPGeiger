/*
  WebPortal.cpp - Async web UI

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
#include "NtpPage.h"
#include "Outputs.h"
#include "Corium/Console/Console.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Prefs/SystemPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/Uptime.h"
#include "Corium/System/CrashDump.h"
#include "Corium/System/BootGuard.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Net/WifiInfo.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/PinSafety.h"
#include <EGEscape.h>
#include <EGHttpForm.h>
#include <EGJson.h>
#include <EGPortal.h>
#include "Corium/Logger/Logger.h"
#include "Corium/NTP/NTP.h"
#include "Corium/System/Clock.h"
#include "Corium/System/Rtc.h"
#include "Corium/System/AppHooks.h"
#include <EGBase64.h>

#ifndef ESP8266
  #include <Update.h>   // esp32 needs it
#endif

#include "STYLE_CSS.gz.h"
#include "THEME_JS.gz.h"

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
.card{background:var(--card);border:1px solid var(--border);border-radius:4px;padding:1em;margin:1em 0}
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
.mt{margin-top:.4em}
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
.devhead h1 .tag{font-size:.6em;font-weight:500;color:var(--muted);margin-left:.4em;opacity:.85}
.devhead .sub{color:var(--muted);font-size:.95em;margin-top:.2em}
.cred{margin-top:1.5em;font-size:.85em;color:var(--muted)}
.cred a{color:inherit}
.cred .brand{color:var(--fg);font-weight:bold;font-family:"Courier New",Courier,monospace}
)CSS";
#endif

#ifndef APP_ICON_SVG
#define APP_ICON_SVG "<circle cx='256' cy='256' r='256' fill='#666'/>"   // app sets it
#endif
#ifndef APP_URL
#define APP_URL "/"
#endif
#ifndef APP_MODEL
#define APP_MODEL ""
#endif

// one flash copy
static const char FAVICON_PATHS[] PROGMEM = APP_ICON_SVG;

static const char HOME_NAME[] PROGMEM = THING_NAME;
static PGM_P s_home = HOME_NAME;

void corium::WebPortal::setHomeName(PGM_P name) { s_home = name; }

static const char BACK_ROW[] PROGMEM =
  "<p class=back-row><a class=back href=/>\xe2\x86\x90 Home</a></p>";
static const char HTML_END[] PROGMEM = "</body></html>";

static const char HEAD_OPEN[] PROGMEM =
  "<!DOCTYPE html><html lang=en><head>"
  "<meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<link rel=icon type=image/svg+xml href=/favicon.svg>";
static const char HEAD_STYLE[] PROGMEM =
  "<link rel=stylesheet href=/style.css" CORIUM_CACHE_BUST "><title>";
static const char SAFE_STYLE[] PROGMEM =
  "<style>html{background:#161616;border-top:12px solid;"
  "border-image:repeating-linear-gradient(45deg,#fb0 0 10px,#161616 0 20px) 12}"
  "body{color:#ddd;font-family:monospace;max-width:420px;margin:0;padding:0 1em}"
  "h1{font-size:1.3em}h1 svg{vertical-align:middle;margin-right:.4em}h1 b,a{color:#fb0}.menu a{display:block;margin:.8em 0}"
  "pre{white-space:pre-wrap}label,small{display:block}label{margin-top:.6em}small{color:#999}"
  ".il{display:inline-block;margin-right:1em}nav a{margin-right:1em}</style><title>";

static const char HEAD_THEME[] PROGMEM =
  "</title><script src=/theme.js" CORIUM_CACHE_BUST "></script></head><body>"
  "<button class=themetoggle aria-label=\"Toggle theme\" onclick=theme()></button>";
static const char SAFE_THEME[] PROGMEM =
  " (safe mode)</title><script>var byID=t=>document.getElementById(t)</script></head><body>";
static const char HEAD_H1[] PROGMEM =
  "<div class=devhead>"
  "<h1><svg viewBox='0 0 512 512' style='width:1.4em;height:1.4em;flex:0 0 auto'>";

static const char SVG_END[] PROGMEM = "</svg>";

static void send_head_open(EGHttpResponse& res) {
  res.sendChunk(FPSTR(HEAD_OPEN));
  res.sendChunk(FPSTR(corium::BootGuard::safeMode() ? SAFE_STYLE : HEAD_STYLE));
}

// no clock yet, the browser has one
static const char CLOCK_JS[] PROGMEM =
  "<script>fetch('/clock',{method:'POST',body:new URLSearchParams({e:Math.floor(Date.now()/1e3)})})</script>";

static void send_head_tail(EGHttpResponse& res) {
  res.sendChunk(FPSTR(corium::BootGuard::safeMode() ? SAFE_THEME : HEAD_THEME));
  if (!corium::Clock::synced) res.sendChunk(FPSTR(CLOCK_JS));
  res.sendChunk(FPSTR(HEAD_H1));
  res.sendChunk(FPSTR(FAVICON_PATHS));
  res.sendChunk(FPSTR(SVG_END));
}

static void send_safe_tag(EGHttpResponse& res) {
  if (corium::BootGuard::safeMode()) res.sendChunk(F(" <b>SAFE MODE</b>"));
}

// n from snprintf, cut text is dropped
static void send_fmt(EGHttpResponse& res, const char* buf, int n, size_t cap) {
  if (n > 0 && (size_t)n < cap) res.sendChunk(buf, (size_t)n);
}

static void send_safe_banner(EGHttpResponse& res) {
  if (!corium::BootGuard::safeMode()) return;
  uint32_t up = corium::fastMillis();
  uint32_t at = corium::BootGuard::retryAfterMs();
  char b[160];
  // minutes left, rounded up
  send_fmt(res, b, snprintf_P(b, sizeof(b),
      PSTR("<p>Normal mode again in %u min, or <a href=/restart>restart now</a>.</p>"),
      (unsigned)(up < at ? (at - up) / 60000 + 1 : 0)), sizeof(b));
}

// after the app's own
static const corium::MenuEntry WEBPORTAL_TOP_MENU[] PROGMEM = {
  {"/param",   "Config"},
  {"/network", "Network Config"},
  {"", ""}
};
static const corium::MenuEntry WEBPORTAL_BOTTOM_MENU[] PROGMEM = {
  {"/info",    "Info"},
  {"/update",  "Update"},
  {"/restart", "Reboot"},
  {"", ""}
};

static void render_menu(EGHttpResponse& res, const corium::MenuEntry* e) {
  for (; e; e++) {
    corium::MenuEntry m;
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

void corium::WebPortal::requestRestart(uint32_t delay_ms) {
  corium::Lifecycle::requestRestart(corium::fastMillis(), delay_ms);
}

// poll() applies these, page drains first
static bool s_wifi_save = false;   // creds in Wifi::holdCreds
static bool s_ap_test = false;     // hotspot mode, the join test runs in place

static bool s_erase_creds = false;
static bool s_factory_reset = false;

static corium::WebPortal* s_portal = nullptr;

#define WIFI_MOVED_MS 4000   // the page polls each 2 s, it sees OK first

// joined from hotspot mode: off it, onto the network; failed: hotspot mode stays, the page said why
static void ap_test_end() {
  s_ap_test = false;
  if (corium::Wifi::testState() != EGPortal::JOIN_OK) return;
  if (!corium::Wifi::joinMode() || !corium::Prefs::commit()) {
    corium::Log::console(PSTR("WebPortal: net settings not saved"));
    return;
  }
  corium::Log::console(PSTR("WebPortal: WiFi moved to '%s', restarting"), corium::Wifi::testSsid());
  corium::WebPortal::requestRestart(WIFI_MOVED_MS);
}

void corium::WebPortal::poll(uint32_t now) {
  // unsigned diff, wrap safe
  if (now - _poll_last_ms < POLL_MS) return;
  _poll_last_ms = now;

  if (_active) _http.tick();
  if (s_ap_test && !corium::Wifi::testing()) ap_test_end();

  if (corium::Lifecycle::restartDue(now)) {
    if (s_wifi_save) {
      corium::Log::console(PSTR("WebPortal: applying new WiFi creds (ssid='%s')"),
                   corium::Wifi::heldSsid());
      (void)corium::Wifi::beginHeld();   // held since the save
      s_wifi_save = false;
      delay(150);  // restart follows, NVS settle
    }
    if (s_erase_creds) {
      corium::Wifi::clearSavedCreds();
      s_erase_creds = false;
      delay(150);  // restart follows
    }
    if (s_factory_reset) {
      corium::DeviceInfo::factoryReset();
      s_factory_reset = false;
      delay(150);  // restart follows
    }
    corium::Log::console(PSTR("WebPortal: restarting"));
    corium::DeviceInfo::safeRestart(50);
  }
}


static const char P_PARAM[]   PROGMEM = "/param";
static const char P_UPDATE[]  PROGMEM = "/update";
static const char P_IMPORT[]  PROGMEM = "/import";
static const char P_RESTART[] PROGMEM = "/restart";

#ifndef SAFE_HTTP_CLIENTS
#define SAFE_HTTP_CLIENTS 3   // page, favicon, one spare
#endif

static const char R_STYLE[]    PROGMEM = "/style.css";
static const char R_FAV_SVG[]  PROGMEM = "/favicon.svg";
static const char R_FAV_ICO[]  PROGMEM = "/favicon.ico";
static const char R_THEME[]    PROGMEM = "/theme.js";
static const char R_GUARD[]    PROGMEM = "/guard";
static const char R_RESET[]    PROGMEM = "/reset";
static const char R_ERASE[]    PROGMEM = "/erase";
static const char R_INFO[]     PROGMEM = "/info";
static const char R_ABOUT[]    PROGMEM = "/about";
static const char R_CS[]       PROGMEM = "/cs";
static const char R_OUTPUTS[]  PROGMEM = "/outputs";
static const char R_METRICS[]  PROGMEM = "/metrics";
static const char R_VARS[]     PROGMEM = "/vars.json";
static const char R_PROGRESS[] PROGMEM = "/update/progress";
static const char R_NETWORK[]  PROGMEM = "/network";
static const char R_SCAN[]     PROGMEM = "/wifiscan";
static const char R_SAVE[]     PROGMEM = "/wifisave";
static const char R_JOIN[]     PROGMEM = "/wifijoin";
static const char R_EXPORT[]   PROGMEM = "/export";
static const char R_CLOCK[]    PROGMEM = "/clock";

// only while there's no other
static void hClock(EGHttpRequest& req, EGHttpResponse& res, void*) {
  uint64_t e = 0;
  if (!corium::Clock::synced && parseU64(req.arg(F("e")), &e) && e >= corium::Clock::MIN_EPOCH && e <= corium::Clock::MAX_EPOCH) {
    corium::Clock::set((time_t)e, corium::Clock::WEB);
    corium::Log::console(PSTR("Clock: set from the browser"));
  }
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
}

void corium::WebPortal::on(EGHttpServer& http, const corium::WebRoute* rows, size_t n) {
  for (size_t i = 0; i < n; i++) {
    corium::WebRoute r;
    memcpy_P(&r, &rows[i], sizeof(r));
    http.on(FPSTR(r.path), r.method, r.h, nullptr);
  }
}

void corium::WebPortal::begin(uint16_t port) {
  if (_active) return;
  using R = EGHttpRequest;
  static const corium::WebRoute ROUTES[] PROGMEM = {
    {R_STYLE,    &corium::WebPortal::hStyleCss,       R::GET},
    {R_FAV_SVG,  &corium::WebPortal::hFavicon,        R::GET},
    {R_FAV_ICO,  &corium::WebPortal::hFavicon,        R::GET},
    {R_THEME,    &corium::WebPortal::hThemeJs,        R::GET},
    {P_RESTART,  &corium::WebPortal::hRestart,        R::GET},
    {R_GUARD,    &corium::WebPortal::hGuard,          R::GET},
    {R_RESET,    &corium::WebPortal::hReset,          R::POST},
    {R_ERASE,    &corium::WebPortal::hEraseWifi,      R::POST},
    {R_INFO,     &corium::WebPortal::hInfo,           R::GET},
    {R_ABOUT,    &corium::WebPortal::hAbout,          R::GET},
    {R_CS,       &corium::WebPortal::hConsoleStream,  R::GET},
    {R_OUTPUTS,  &corium::WebPortal::hOutputs,        R::GET},
    {R_METRICS,  &corium::WebPortal::hMetrics,        R::GET},
    {R_VARS,     &corium::WebPortal::hVars,           R::GET},
    {P_PARAM,    &corium::WebPortal::hParam,          R::GET},
    {P_PARAM,    &corium::WebPortal::hParam,          R::POST},
    {P_UPDATE,   &corium::WebPortal::hUpdateForm,     R::GET},
    {P_UPDATE,   &corium::WebPortal::hUpdateDone,     R::POST},
    {R_PROGRESS, &corium::WebPortal::hUpdateProgress, R::GET},
    {R_NETWORK,  &corium::WebPortal::hWifi,           R::GET},
    {R_SCAN,     &corium::WebPortal::hWifiScan,       R::GET},
    {R_SAVE,     &corium::WebPortal::hWifiSave,       R::POST},
    {R_JOIN,     &corium::WebPortal::hWifiJoin,       R::GET},
    {R_EXPORT,   &corium::WebPortal::hExport,         R::GET},
    {P_IMPORT,   &corium::WebPortal::hImport,         R::POST},
    {R_CLOCK,    hClock,                      R::POST},
  };
  _http.on(F("/"), R::GET, &corium::WebPortal::hRoot, this);
  on(_http, ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
  bool safe = corium::BootGuard::safeMode();
  if (safe) _http.setMaxClients(SAFE_HTTP_CLIENTS);
  else _http.on(F("/webcmd"), R::POST, &corium::WebPortal::hWebCmd, nullptr);
  _http.onUpload(FPSTR(P_PARAM), &corium::WebPortal::hParamBody, nullptr);
  _http.onUpload(FPSTR(P_UPDATE), &corium::WebPortal::hUpdateBody, nullptr);
  _http.onUpload(FPSTR(P_IMPORT), &corium::WebPortal::hImportBody, nullptr);

  if (!safe) corium::AppHooks::routes(_http);

  // Module routes
  uint8_t n = corium::Registry::count();
  for (uint8_t i = 0; i < n; i++) {
    corium::Module* m = corium::Registry::get(i);
    if (m && !corium::Registry::quarantined(m)) m->registerRoutes(_http);
  }
  if (!corium::Registry::quarantined(&ntpclient)) corium::NtpPage::routes(_http);

  _http.begin(port);
  _active = true;
  s_portal = this;
  applyAuthFromPrefs();
}

void corium::WebPortal::applyAuthFromPrefs() {
  corium::WebPortal* p = s_portal;
  if (!p || !p->_active) return;
  const char* cur = corium::Prefs::getString("sys", corium::K_WEB_PASS);
  static_assert(sizeof("admin") - 1 + 1 + WEB_PASS_MAX <= EGHTTP_AUTH_MAX,
                "admin:pass must fit EGHTTP_AUTH_MAX or auth switches off");
  p->_http.setBasicAuth(F("admin"), cur, F(THING_NAME));
  static int8_t s_auth = -1;
  int8_t auth = (cur && *cur) ? 1 : 0;
  if (auth != s_auth) {
    corium::Log::console(PSTR("WebPortal: auth %s"),
                 auth ? PSTR("enabled (user=admin)") : PSTR("disabled"));
    s_auth = auth;
  }
}

void corium::WebPortal::sendPageHead(EGHttpResponse& res, const __FlashStringHelper* title,
                             const char* inlineSub) {
  const char* name = corium::DeviceInfo::displayName();
  char esc[6 * CORIUM_NAME_MAX + 1];
  egesc_html(esc, sizeof(esc), name);
  send_head_open(res);
  res.sendChunk(title);
  res.sendKV(F(" \xc2\xb7 "), esc);
  send_head_tail(res);
  res.sendChunk(F(THING_NAME " - "));
  res.sendChunk(title);
  if (inlineSub && inlineSub[0]) {
    egesc_html(esc, sizeof(esc), inlineSub);
    res.sendKV(F("<span class=tag>"), esc, F("</span>"));
  }
  send_safe_tag(res);
  res.sendChunk(F("</h1></div>"));
  send_safe_banner(res);
}

void corium::WebPortal::sendPageTail(EGHttpResponse& res, bool footer) {
  res.sendChunk(FPSTR(BACK_ROW));
  if (footer) corium::WebPortal::sendPageFooter(res);
  res.sendChunk(FPSTR(HTML_END));
}

void corium::WebPortal::beginPage(EGHttpResponse& res, const __FlashStringHelper* title,
                          const char* inlineSub) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  sendPageHead(res, title, inlineSub);
}

void corium::WebPortal::endPage(EGHttpResponse& res, bool footer) {
  sendPageTail(res, footer);
  res.endChunked();
}

void corium::WebPortal::sendPageFooter(EGHttpResponse& res) {
  res.sendChunk(F("<p class=cred>"
                  "<a class=brand href='" APP_URL "' target=_blank rel=noopener>" THING_NAME "</a>"
                  " &middot; <a href='/about'>" RELEASE_VERSION "</a></p>"));
}

static void root_safe_note(EGHttpResponse& res) {
  res.sendChunk(corium::BootGuard::crashStreak() ? F("<pre>" THING_NAME " keeps crashing.\n")
                                          : F("<pre>Safe mode was requested.\n"));
  res.sendChunk(F("Undo a recent change in Config or install a previous firmware via Update.\n"
                  "Info shows the latest crash for bug reports.</pre>"));
}

static void root_menus(EGHttpResponse& res, bool safe) {
  res.sendChunk(F("<div class=menu>"));
  if (!safe) render_menu(res, corium::AppHooks::menu());
  render_menu(res, WEBPORTAL_TOP_MENU);
  uint8_t mc = corium::Registry::count();
  for (uint8_t i = 0; i < mc; i++) {
    corium::Module* m = corium::Registry::get(i);
    if (m && !corium::Registry::quarantined(m)) render_menu(res, m->menuEntries());
  }
  render_menu(res, WEBPORTAL_BOTTOM_MENU);
  res.sendChunk(F("</div>"));
}

void corium::WebPortal::hRoot(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));

  const char* fname = corium::DeviceInfo::friendlyName();
  bool named = (fname && fname[0]);
  const char* hostname = corium::DeviceInfo::hostname();
  char ip[CORIUM_IPV4_MAX + 1];
  IPAddress ipa = corium::Wifi::localIP();
  if (!ipa[0]) ipa = corium::Wifi::softAPIP();   // wifi.mode 2
  corium::Wifi::formatIP(ip, sizeof(ip), ipa);

  char esc[6 * CORIUM_NAME_MAX + 1];
  egesc_html(esc, sizeof(esc), fname);

  auto identity = [&]() {
    res.sendChunk(FPSTR(s_home));
    if (named) res.sendKV(F(" - "), esc);
  };

  bool safe = corium::BootGuard::safeMode();
  send_head_open(res);
  identity();
  send_head_tail(res);
  identity();
  send_safe_tag(res);
  res.sendKV(F("</h1><div class=sub>"), hostname);
  res.sendKV(F(" &middot; "), ip, F("</div></div>"));
  send_safe_banner(res);
  if (safe) root_safe_note(res);
  root_menus(res, safe);
  if (!safe) corium::WebPortal::sendPageFooter(res);
  res.sendChunk(FPSTR(HTML_END));
  res.endChunked();
}

void corium::WebPortal::sendGz(EGHttpResponse& res, const __FlashStringHelper* type,
                       const uint8_t* gz, size_t len) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.sendGzipP(200, type, gz, len);
}

void corium::WebPortal::hStyleCss(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_STYLE_CSS
  sendGz(res, F("text/css"), STYLE_CSS_GZ, STYLE_CSS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.beginChunked(200, F("text/css"));
  res.sendChunk(FPSTR(STYLE_CSS));
  res.sendChunk(FPSTR(APP_CSS));
  res.endChunked();
#endif
}

void corium::WebPortal::hFavicon(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), F(CACHE_7D));
  res.beginChunked(200, F("image/svg+xml"));
  res.sendChunk(F("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>"));
  res.sendChunk(FPSTR(FAVICON_PATHS));
  res.sendChunk(FPSTR(SVG_END));
  res.endChunked();
}

#if !EG_GZ_THEME_JS
// live(url or fn, ms, fn, first): fn(text), null on error, returns the next ms or nothing; gives kick(ms)
static const char THEME_JS[] PROGMEM = R"JS(var byID=t=>document.getElementById(t);
!function(){
var d=document.documentElement,LS=localStorage;
window.theme=()=>{var t=d.dataset.theme=='dark'?'light':'dark';
d.dataset.theme=LS.theme=t;dispatchEvent(new Event('themechange'))};
d.dataset.theme=LS.theme||(matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
}();
window.live=(u,ms,fn,at)=>{var J,L=0,
k=w=>{clearTimeout(J);J=setTimeout(go,w)},
go=()=>fetch(typeof u=='function'?u():u,{cache:'no-store'}).then(r=>r.ok?r.text():null,()=>null).then(t=>{L=Date.now();var w;try{w=fn(t)}catch(e){}k(w>=0?w:t==null?6e3:ms)});
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&L)k(Math.max(0,ms-(Date.now()-L)))});
if(at>=0)k(at);return k};
)JS";
#endif

void corium::WebPortal::hThemeJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_THEME_JS
  sendGz(res, FPSTR(HTTP_CT_JS), THEME_JS_GZ, THEME_JS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.beginChunked(200, FPSTR(HTTP_CT_JS));
  res.sendChunk(FPSTR(THEME_JS));
  res.sendChunk(FPSTR(APP_THEME_JS));
  res.endChunked();
#endif
}

#define RESTART_COUNTDOWN_SECS 8
#define WIFI_RESTART_MS 2500
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

void corium::WebPortal::sendRestartCountdown(EGHttpResponse& res) {
  char buf[40];
  send_fmt(res, buf, snprintf_P(buf, sizeof(buf), PSTR("<script>window._rs=%u;</script>"),
                                (unsigned)RESTART_COUNTDOWN_SECS), sizeof(buf));
  res.sendChunk(FPSTR(RESTART_COUNTDOWN));
}

static const char WRITE_FAILED[] PROGMEM = "flash write failed";

// a group holding a value that didn't load says which
void corium::WebPortal::sendNotSaved(EGHttpResponse& res) {
  res.sendChunk(F("<div class='a ae'>Not saved: "));
  char why[CORIUM_PREFS_FROZEN_MAX];
  if (corium::Prefs::frozen(why, sizeof(why))) res.sendChunk(why);
  else res.sendChunk(FPSTR(WRITE_FAILED));
  res.sendChunk(F(".</div>"));
}

static void not_saved_page(EGHttpResponse& res, const __FlashStringHelper* title) {
  corium::WebPortal::beginPage(res, title);
  corium::WebPortal::sendNotSaved(res);
  corium::WebPortal::endPage(res);
}

void corium::WebPortal::hRestart(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Restarting"));
  res.sendChunk(F("<p>Device is restarting. This page will reload once it's back.</p>"));
  corium::WebPortal::sendRestartCountdown(res);
  corium::WebPortal::endPage(res);
  corium::WebPortal::requestRestart();
}

void corium::WebPortal::hGuard(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* e = req.arg(F("enter"));
  if (!e || strcmp_P(e, PSTR("1")) != 0) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("/guard?enter=1 restarts into safe mode"));
    return;
  }
  corium::BootGuard::requestSafe();
  hRestart(req, res, nullptr);
}

// /info

static const char IR_BYTES[] PROGMEM = "%u bytes";
static const char IR_HEX[]   PROGMEM = "0x%08x";
static const char IR_CODE_END[] PROGMEM = "</code></td></tr>";
static const char IR_END[]   PROGMEM = "</table></details>";

void corium::WebPortal::infoRow(EGHttpResponse& res, PGM_P key, PGM_P fmt, ...) {
  char row[320];
  size_t pos = 0;
  advancePos(pos, snprintf_P(row, sizeof(row), PSTR("<tr><th>%s</th><td>"), key), sizeof(row));
  va_list ap;
  va_start(ap, fmt);
  advancePos(pos, vsnprintf_P(row + pos, sizeof(row) - pos, fmt, ap), sizeof(row));
  va_end(ap);
  advancePos(pos, snprintf_P(row + pos, sizeof(row) - pos, PSTR("</td></tr>")), sizeof(row));
  res.sendChunk(row, pos);
}

#define INFO_ROW(k, fmt, ...) corium::WebPortal::infoRow(res, PSTR(k), fmt, ##__VA_ARGS__)

struct InfoNet {
  char ip[CORIUM_IPV4_MAX + 1], dns[CORIUM_IPV4_MAX + 1];
  char mac[18];
  char ssid[6 * EGPORTAL_SSID_MAX + 1];   // escaped worst case
  char ap_ssid[EGPORTAL_SSID_MAX + 1], ap_ip[CORIUM_IPV4_MAX + 1], ap_mac[18];
};

static void info_fmt_mac(char* dst, size_t cap, const uint8_t* m) {
  snprintf_P(dst, cap, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void info_collect_net(InfoNet& net) {
  corium::Wifi::formatIP(net.ip,    sizeof(net.ip),    corium::Wifi::localIP());
  corium::Wifi::formatIP(net.dns,   sizeof(net.dns),   corium::Wifi::dnsIP());
  corium::Wifi::formatIP(net.ap_ip, sizeof(net.ap_ip), corium::Wifi::softAPIP());
  uint8_t mac[6];
  corium::Wifi::macAddress(mac);
  info_fmt_mac(net.mac, sizeof(net.mac), mac);
  corium::Wifi::softAPmacAddress(mac);
  info_fmt_mac(net.ap_mac, sizeof(net.ap_mac), mac);
  egesc_html(net.ssid, sizeof(net.ssid), corium::Wifi::SSID().c_str());
  strlcpy(net.ap_ssid, corium::Wifi::softAPSSID().c_str(), sizeof(net.ap_ssid));
}

// "0x%08x " per word, longer than infoRow's buffer
static constexpr size_t FRAMES_MAX = corium::CrashDump::STACK_WORDS * 11 + 1;

#ifdef ESP8266
static void info_render_frames(EGHttpResponse& res, const corium::CrashDump::Snapshot* cd) {
  // code addresses only, iram and flash
  char frames[FRAMES_MAX];
  size_t pos = 0;
  bool any = false;
  for (uint32_t i = 0; i < cd->wordCount && pos < sizeof(frames) - 12; i++) {
    uint32_t w = cd->stack[i];
    if (w < 0x40100000u || w >= 0x40300000u) continue;
    int wn = snprintf_P(frames + pos, sizeof(frames) - pos,
                        PSTR("%s0x%08x"), any ? PSTR(" ") : PSTR(""), (unsigned)w);
    if (wn > 0 && (size_t)wn < sizeof(frames) - pos) pos += (size_t)wn;
    any = true;
  }
  if (any) {
    frames[pos] = '\0';
    res.sendKV(F("<tr><th>Stack frames</th><td><code>"), frames, FPSTR(IR_CODE_END));
  }
}

static void info_render_reset(EGHttpResponse& res) {
  INFO_ROW("Last reset",    W_FMT_S, corium::DeviceInfo::resetText());
  uint32_t epc1, excvaddr;
  uint8_t exccause;
  if (corium::DeviceInfo::resetExc(&epc1, &excvaddr, &exccause)) {
    INFO_ROW("Exc cause", W_FMT_U,   (unsigned)exccause);
    INFO_ROW("Exc PC",    IR_HEX, (unsigned)epc1);
    INFO_ROW("Exc addr",  IR_HEX, (unsigned)excvaddr);
  }
  if (const corium::CrashDump::Snapshot* cd = corium::CrashDump::lastCrash()) {
    INFO_ROW("EPC2",      IR_HEX, (unsigned)cd->epc2);
    INFO_ROW("EPC3",      IR_HEX, (unsigned)cd->epc3);
    INFO_ROW("DEPC",      IR_HEX, (unsigned)cd->depc);
    INFO_ROW("SP",        IR_HEX, (unsigned)cd->sp);
    info_render_frames(res, cd);
  }
}
#elif defined(ESP32)
static void info_render_backtrace(EGHttpResponse& res, const corium::CrashDump::Snapshot* cd) {
  char frames[FRAMES_MAX];
  size_t pos = 0;
  for (uint32_t i = 0; i < cd->wordCount && pos < sizeof(frames) - 12; i++) {
    int wn = snprintf_P(frames + pos, sizeof(frames) - pos,
                        PSTR("%s0x%08x"), i ? " " : "", (unsigned)cd->stack[i]);
    if (wn > 0 && (size_t)wn < sizeof(frames) - pos) pos += (size_t)wn;
  }
  frames[pos] = '\0';
#ifdef __riscv
  res.sendKV(F("<tr><th>Stack</th><td><code>"), frames, FPSTR(IR_CODE_END));   // raw words, not PCs
#else
  res.sendKV(F("<tr><th>Backtrace</th><td><code>"), frames, FPSTR(IR_CODE_END));
#endif
}

static void info_render_reset(EGHttpResponse& res) {
  INFO_ROW("Last reset", W_FMT_S, corium::DeviceInfo::resetText());
  if (const char* in = corium::Registry::crashedIn()) INFO_ROW("Crashed in", W_FMT_S, in);
  if (const corium::CrashDump::Snapshot* cd = corium::CrashDump::lastCrash()) {
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
    if (cd->wordCount) info_render_backtrace(res, cd);
  }
}
#endif

static void info_uptime(EGHttpResponse& res) {
  corium::Uptime::Split u = corium::Uptime::split((uint32_t)corium::Uptime::seconds());
  if (u.d)      INFO_ROW("Uptime", PSTR("%u d %u h"), (unsigned)u.d, (unsigned)u.h);
  else if (u.h) INFO_ROW("Uptime", PSTR("%u h %u min"), (unsigned)u.h, (unsigned)u.m);
  else          INFO_ROW("Uptime", PSTR("%u min"), (unsigned)u.m);
}

// who set the clock
static void info_time(EGHttpResponse& res) {
  PGM_P src = PSTR("not set");
  if (corium::Clock::source == corium::Clock::NTP)       src = PSTR("NTP");
  else if (corium::Clock::source == corium::Clock::KEPT) src = PSTR("kept over a restart");
  else if (corium::Clock::source == corium::Clock::WEB)  src = PSTR("browser");
#ifdef CORIUM_RTC
  if (corium::Rtc::chip()) {
    if (corium::Clock::source == corium::Clock::RTC) INFO_ROW("Time", PSTR("RTC (%s)"), corium::Rtc::chip());
    else INFO_ROW("Time", PSTR("%s, RTC %s"), src, corium::Rtc::chip());
    return;
  }
#endif
  INFO_ROW("Time", W_FMT_S, src);
}

static void info_render_system(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>System</summary><table>"));
  info_uptime(res);
  info_time(res);
  INFO_ROW("Chip ID",       W_FMT_S, corium::DeviceInfo::chipId());   // udp pairing asks for it
  uint32_t heap = corium::DeviceInfo::freeHeap();
  // KB with one decimal, no float
  INFO_ROW("Free heap",     PSTR("%u.%u KB"), (unsigned)(heap / 1024), (unsigned)(heap % 1024 * 10 / 1024));
  INFO_ROW("Firmware",      PSTR("%u KB of %u KB"), (unsigned)(corium::DeviceInfo::sketchSize() / 1024),
                            (unsigned)((corium::DeviceInfo::sketchSize() + corium::DeviceInfo::freeSketch()) / 1024));
#ifdef ESP8266
  if (corium::DeviceInfo::flashSize() != corium::DeviceInfo::realFlash()) {
    INFO_ROW("Flash", PSTR("%u KB set, the chip has %u KB"), (unsigned)(corium::DeviceInfo::flashSize() / 1024),
             (unsigned)(corium::DeviceInfo::realFlash() / 1024));
  }
#endif
  info_render_reset(res);
  if (corium::BootGuard::safeMode()) INFO_ROW("Safe mode", PSTR("yes"));
  if (corium::BootGuard::crashStreak()) INFO_ROW("Crash boots in a row", W_FMT_U, (unsigned)corium::BootGuard::crashStreak());
  if (corium::SystemPrefs::passRestored) INFO_ROW("Web password", PSTR("restored from backup (settings lost)"));
  res.sendChunk(FPSTR(IR_END));
}

// -80 and weaker drops out
static PGM_P rssi_word(int r) {
  if (r >= -60) return PSTR("excellent");
  if (r >= -70) return PSTR("good");
  if (r >= -80) return PSTR("fair");
  return PSTR("weak");
}

static void info_render_wifi(EGHttpResponse& res, const InfoNet& net) {
  res.sendChunk(F("<details open><summary>WiFi</summary><table>"));
  if (net.ssid[0]) INFO_ROW("Network", PSTR("%s, channel %d"), net.ssid, (int)corium::Wifi::channel());
  if (corium::Wifi::isConnected()) INFO_ROW("RSSI", PSTR("%d dBm (%s)"), (int)corium::Wifi::rssi, rssi_word(corium::Wifi::rssi));
  INFO_ROW("IP",             W_FMT_S,     net.ip);
  INFO_ROW("Hostname",       W_FMT_S,     corium::DeviceInfo::hostname());
  INFO_ROW("MAC",            W_FMT_S,     net.mac);
  INFO_ROW("DNS",            W_FMT_S,     net.dns);
  if (strcmp_P(net.ap_ip, PSTR("0.0.0.0"))) {   // ssid is kept with the AP off
    INFO_ROW("AP SSID",      W_FMT_S,     net.ap_ssid);
    INFO_ROW("AP IP",        W_FMT_S,     net.ap_ip);
    INFO_ROW("AP MAC",       W_FMT_S,     net.ap_mac);
  }
  res.sendChunk(FPSTR(IR_END));
}

static void info_render_about(EGHttpResponse& res) {
  res.sendChunk(F("<details open><summary>About</summary><table>"));
  INFO_ROW("Build", PSTR(RELEASE_VERSION " " GIT_VERSION " " BUILD_ENV " %s"),
           corium::DeviceInfo::buildDate());
  corium::AppHooks::infoRows(res);
  res.sendChunk(FPSTR(IR_END));
}

#undef INFO_ROW

void corium::WebPortal::hInfo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Info"));

  InfoNet net;
  info_collect_net(net);

  if (net.ssid[0]) res.sendKV(F("<p class=muted>Connected to <b>"), net.ssid);
  else             res.sendChunk(F("<p class=muted>Connected to <b>(not connected)"));
  res.sendKV(F("</b> with IP <b>"), net.ip);
  res.sendKV(F("</b> &middot; "), corium::DeviceInfo::board(), F("</p>"));

  info_render_system(res);
  info_render_wifi(res, net);
  info_render_about(res);

  corium::WebPortal::endPage(res);
}

static void about_modules(EGHttpResponse& res) {
  uint8_t count = corium::Registry::count();
  for (uint8_t i = 0; i < count; i++) {
    corium::Module* m = corium::Registry::get(i);
    if (m == nullptr) continue;
    char buf[40];
    int n = snprintf_P(buf, sizeof(buf),
      PSTR("%s\"%s\""), (i > 0 ? PSTR(",") : PSTR("")), m->name());
    size_t len = 0;
    advancePos(len, n, sizeof(buf));
    if (len) res.sendChunk(buf, len);
  }
}

// what /info leaves out
static void about_device(EGHttpResponse& res) {
  char buf[200];
#ifdef ESP8266
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("\"core\":\"%s\",\"boot\":%u,\"fsize\":%u,\"freal\":%u,"),
    corium::DeviceInfo::coreVersion().c_str(), (unsigned)corium::DeviceInfo::bootVersion(),
    (unsigned)corium::DeviceInfo::flashSize(), (unsigned)corium::DeviceInfo::realFlash());
#elif defined(ESP32)
  char celsius[8] = "null";
  float t = corium::DeviceInfo::chipTempC();
  if (!isnan(t)) snprintf_P(celsius, sizeof(celsius), PSTR("%.1f"), t);
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("\"model\":\"%s\",\"rev\":%u,\"sdk\":\"%s\",\"fsize\":%u,\"temp\":%s,"),
    corium::DeviceInfo::chipModel(), (unsigned)corium::DeviceInfo::chipRev(), corium::DeviceInfo::sdkVersion(),
    (unsigned)corium::DeviceInfo::flashSize(), celsius);
#endif
  size_t len = 0;
  advancePos(len, n, sizeof(buf));
  if (len) res.sendChunk(buf, len);

  char gw[CORIUM_IPV4_MAX + 1], sn[CORIUM_IPV4_MAX + 1], bssid[18] = {}, tx[8];
  corium::Wifi::formatIP(gw, sizeof(gw), corium::Wifi::gatewayIP());
  corium::Wifi::formatIP(sn, sizeof(sn), corium::Wifi::subnetMask());
  if (const uint8_t* b = corium::Wifi::BSSID()) info_fmt_mac(bssid, sizeof(bssid), b);
  int q = corium::Wifi::txPowerQdbm();   // 0.25 dBm steps, -1 = unknown
  if (q >= 0) snprintf_P(tx, sizeof(tx), PSTR("%d.%02d"), q / 4, (q % 4) * 25);
  else flashToRam(PSTR("null"), tx, sizeof(tx));
  n = snprintf_P(buf, sizeof(buf),
    PSTR("\"mhz\":%u,\"gw\":\"%s\",\"sn\":\"%s\",\"bssid\":\"%s\",\"tx\":%s,"
         "\"phy\":\"%s\",\"sleep\":\"%s\",\"auto\":%s,"),
    (unsigned)corium::DeviceInfo::cpuMHz(), gw, sn, bssid, tx, corium::Wifi::phyModeStr(), corium::Wifi::sleepModeStr(),
    corium::Wifi::getAutoReconnect() ? W_TRUE : W_FALSE);
  len = 0;
  advancePos(len, n, sizeof(buf));
  if (len) res.sendChunk(buf, len);
}

void corium::WebPortal::hAbout(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  uint8_t mac[6];
  corium::Wifi::macAddress(mac);
  char hex[13];
  snprintf_P(hex, sizeof(hex), PSTR("%02X%02X%02X%02X%02X%02X"),
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  char buf[320];
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("{\"ver\":\"" RELEASE_VERSION "\",\"git\":\"" GIT_VERSION "\",\"env\":\"" BUILD_ENV "\","
         "\"date\":\"%s\",\"chip\":\"%s\",\"flash\":\"%s\",\"mac\":\"%s\","
         "\"host\":\"%s\","),
    corium::DeviceInfo::buildDate(),
    corium::DeviceInfo::board(),   // "chip" carries the board
    corium::DeviceInfo::flashInfo(),
    hex,
    corium::DeviceInfo::hostname());
  size_t len = 0;
  advancePos(len, n, sizeof(buf));
  if (len) res.sendChunk(buf, len);

  about_device(res);
  corium::AppHooks::aboutJson(res);
  res.sendChunk(F("\"modules\":["));
  about_modules(res);
  res.sendChunk(F("]}"));
  res.endChunked();
}

void corium::WebPortal::hReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  s_factory_reset = true;
  corium::WebPortal::beginPage(res, F("Reset"));
  res.sendChunk(F("<p>All settings cleared. Device is restarting and will return to the setup portal.</p>"));
  corium::WebPortal::sendRestartCountdown(res);
  corium::WebPortal::endPage(res);
  corium::WebPortal::requestRestart();
}

void corium::WebPortal::hEraseWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // back to DHCP for the setup AP, a net row that didn't load goes too
  corium::Log::console(PSTR("WebPortal: erasing WiFi credentials"));
  corium::Prefs::reset("net");
  if (!corium::Prefs::commit()) {
    not_saved_page(res, F("Erase WiFi"));
    return;
  }
  corium::WebPortal::beginPage(res, F("Erase WiFi"));
  res.sendChunk(F("<p>WiFi credentials erased and network reset to DHCP. Device will reboot into Setup Mode.</p>"));
  corium::WebPortal::endPage(res);
  s_erase_creds = true;
  corium::WebPortal::requestRestart();
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
<input id=s name=ssid maxlength=)HTML" STR(EGPORTAL_SSID_MAX) R"HTML( placeholder='(blank to keep current network)'>
<label for=p>Password</label>
<input id=p name=password type=password maxlength=)HTML" STR(EGPORTAL_PASS_MAX) R"HTML( placeholder='(blank for open networks)'>
)HTML";

static const char WIFI_FORM_END[] PROGMEM = R"HTML(
<button type=submit>Save and reboot</button>
</form>
<p class=muted style='margin-top:1em'>If the new network is unreachable, the previous one is restored automatically after about a minute.</p>
)HTML";

static void wifi_ip_field(EGHttpResponse& res, const __FlashStringHelper* head,
                          const char* key) {
  static const char IPV4_PATTERN_ATTR[] PROGMEM =
    " pattern='^(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)(\\.(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)){3}$' value='";
  char esc[6 * CORIUM_IPV4_MAX + 1];
  egesc_html(esc, sizeof(esc), corium::Prefs::getString("net", key));
  res.sendChunk(head);
  res.sendChunk(FPSTR(IPV4_PATTERN_ATTR));
  res.sendKV(nullptr, esc, F("'>"));
}

static void wifi_render_static_ip(EGHttpResponse& res) {
  bool static_on = corium::Prefs::getBool("net", corium::K_STATIC_IP);
  res.sendChunk(F("<details"));
  if (static_on) res.sendChunk(F(" open"));
  res.sendChunk(F("><summary>Network (advanced)</summary>"
                  "<label style='display:flex;align-items:center;gap:.5em'>"
                  "<input type=checkbox name=static_ip value=1"));
  if (static_on) res.sendChunk(FPSTR(W_CHECKED));
  res.sendChunk(F(">Use static IP (otherwise DHCP)</label>"));

  wifi_ip_field(res, F("<label for=ip>IP address</label>"
                       "<input id=ip name=ip maxlength=" STR(CORIUM_IPV4_MAX) " placeholder='192.168.1.50'"), corium::K_IP);
  wifi_ip_field(res, F("<label for=gw>Gateway</label>"
                       "<input id=gw name=gw maxlength=" STR(CORIUM_IPV4_MAX) " placeholder='192.168.1.1'"), corium::K_GW);
  wifi_ip_field(res, F("<label for=sn>Subnet mask</label>"
                       "<input id=sn name=sn maxlength=" STR(CORIUM_IPV4_MAX) " placeholder='255.255.255.0'"), corium::K_SN);
  wifi_ip_field(res, F("<label for=dns>DNS server <span class=muted style='font-weight:normal'>(optional)</span></label>"
                       "<input id=dns name=dns maxlength=" STR(CORIUM_IPV4_MAX) " placeholder='same as gateway'"), corium::K_DNS);
  res.sendChunk(FPSTR(W_DETAILS_END));
}

void corium::WebPortal::hWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Network"));

  char ssid[6 * EGPORTAL_SSID_MAX + 1];
  egesc_html(ssid, sizeof(ssid), corium::Wifi::SSID().c_str());
  if (ssid[0]) res.sendKV(F("<p class=muted>Connected to <b>"), ssid, F("</b></p>"));
  else         res.sendChunk(F("<p class=muted>Connected to <b>(none)</b></p>"));

  res.sendChunk(FPSTR(WIFI_FORM_HEAD));
  wifi_render_static_ip(res);
  res.sendChunk(FPSTR(WIFI_FORM_END));

  if (!corium::Registry::quarantined(&ntpclient)) {
    // lazy load the tz list
    res.sendChunk(F("<details style='margin-top:1.5em' "
                    "ontoggle=\"if(this.open&&!this.dataset.loaded){"
                    "this.dataset.loaded=1;"
                    "var s=document.createElement('script');"
                    "s.src='/ntpjs" CORIUM_CACHE_BUST "';this.appendChild(s);"
                    "}\">"
                    "<summary>Time / NTP</summary>"));
    corium::NtpPage::form(res);
    res.sendChunk(FPSTR(W_DETAILS_END));
  }

  res.sendChunk(FPSTR(wifiAdvancedHTML));
  corium::WebPortal::endPage(res);
}

static void send_aps(EGHttpResponse& res, const EGPortal::ScanEntry* aps, size_t count) {
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

void corium::WebPortal::hWifiScan(EGHttpRequest& req, EGHttpResponse& res, void*) {
  static constexpr uint32_t KEEP_MS = 60000;
  static constexpr uint32_t FORCE_MS = 10000;   // refresh is unauth, radio goes off channel
  static corium::Stamp s_scan;
  static const char SCANNING[] PROGMEM = "{\"state\":\"scanning\"}";
  uint32_t now = corium::fastMillis();
  bool force = req.arg(F("refresh")) != nullptr;
  if (force && s_scan.fresh(now, FORCE_MS)) force = false;

  int status = corium::Wifi::scanComplete();
  if (!force && status == WIFI_SCAN_RUNNING) {
    res.send(200, FPSTR(HTTP_CT_JSON), FPSTR(SCANNING));
    return;
  }

  bool stale = !s_scan.fresh(now, KEEP_MS);
  bool retry = status <= 0 && !s_scan.fresh(now, FORCE_MS);   // none or failed, 10 s apart
  bool rescan = force || retry || stale;
  if (rescan) {
    if (status != WIFI_SCAN_RUNNING) corium::Wifi::scanDelete();
    corium::Wifi::scanNetworks(true /*async*/, false /*show_hidden*/);
    s_scan.mark(now);
  }
  // started, or failed: again 10 s on
  if (rescan || status < 0) {
    res.send(200, FPSTR(HTTP_CT_JSON), FPSTR(SCANNING));
    return;
  }

  EGPortal::ScanEntry aps[12];
  size_t count = EGPortal::processScan(aps, 12, status);
  corium::Wifi::scanDelete();   // aps is the copy, a reload scans again
  send_aps(res, aps, count);
}

static void wifi_render_save_msg(EGHttpResponse& res, bool has_ssid) {
  if (has_ssid) {
    char ssid[6 * EGPORTAL_SSID_MAX + 1];
    egesc_html(ssid, sizeof(ssid), corium::Wifi::heldSsid());
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
                    char (&dst)[CORIUM_IPV4_MAX + 1]) {
  const char* v = req.arg(name);
  if (!v) v = "";
  if (strlcpy(dst, v, sizeof(dst)) >= sizeof(dst)) return false;
  IPAddress a;
  return !*dst || a.fromString(dst);
}

// BUSY is the sdk's
enum HoldResult : uint8_t { HELD, TOO_LONG, BAD_PASS, NO_MEMORY, IN_TEST };

// arg() reuses its buffer, copy the ssid first
static HoldResult wifi_hold_args(EGHttpRequest& req, const char* ssid) {
  char s[EGPORTAL_SSID_MAX + 1];
  if (strlcpy(s, ssid, sizeof(s)) >= sizeof(s)) return TOO_LONG;
  const char* pass = req.arg(F("password"));
  if (pass && strlen(pass) > EGPORTAL_PASS_MAX) return TOO_LONG;
  if (!EGPortal::passOk(pass)) return BAD_PASS;
  switch (corium::Wifi::holdCreds(s, pass)) {
    case corium::Wifi::HOLD_OK:   return HELD;
    case corium::Wifi::HOLD_BUSY: return IN_TEST;   // a join test, or a save waiting for its restart
    default:                      return NO_MEMORY;
  }
}

// a bad SSID or password said, false
static bool wifi_held_ok(EGHttpResponse& res, HoldResult held) {
  if (held == IN_TEST) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("Already testing a network, wait for the result.\n"));
    return false;
  }
  if (held == TOO_LONG) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad WiFi settings: SSID or password too long\nClick Back to fix it.\n"));
    return false;
  }
  if (held == BAD_PASS) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad WiFi settings: password must be 8-63 characters, or 64 hex digits\nClick Back to fix it.\n"));
    return false;
  }
  if (held == NO_MEMORY) {
    res.send(500, FPSTR(HTTP_CT_PLAIN), F("WiFi settings not saved: out of memory\n"));
    return false;
  }
  return true;
}

// true = the 400 sent
static bool static_refused(EGHttpResponse& res, const char* ip, const char* gw, const char* sn) {
  char err[96];
  if (corium::Wifi::validateStatic(ip, gw, sn, err, sizeof(err))) return false;
  char body[160];
  int n = snprintf_P(body, sizeof(body),
    PSTR("Bad network config: %s\nClick Back to fix it.\n"), err);
  if (n > 0) res.send(400, FPSTR(HTTP_CT_PLAIN), body);
  else       res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad network config"));
  return true;
}

// backup first, boot can revert: a static IP with none to go back to could lock it out.
// false = not all put, the rest still go as /param's
CORIUM_NODISCARD static bool put_net(bool st, bool join, const char* ip, const char* gw,
                                     const char* sn, const char* dns) {
  if (!corium::Wifi::saveNetBackup() && st) return false;
  const char* f[] = {st ? "1" : "0", ip, gw, sn, dns};
  // hotspot mode: only once joined
  return corium::Wifi::putNet(f) && (!join || corium::Wifi::ap || corium::Wifi::joinMode());
}

static void ap_test_save(EGHttpResponse& res);

// the 0.13 way: saved, restart, the backup reverts a network that fails;
// hotspot mode tests it here, the browser is on the unit's AP and sees the result
static void wifi_saved(EGHttpResponse& res, bool has_ssid) {
  if (has_ssid) {
    if (corium::Wifi::ap) {
      ap_test_save(res);
      return;
    }
    (void)corium::Wifi::saveBackupCreds();   // logged; the portal is the way back
    s_wifi_save = true;
  }
  corium::WebPortal::beginPage(res, F("Saving WiFi"));
  wifi_render_save_msg(res, has_ssid);
  corium::WebPortal::sendRestartCountdown(res);
  corium::WebPortal::endPage(res);
  corium::WebPortal::requestRestart(WIFI_RESTART_MS);
}

// hotspot mode: tested in place, saved and off the AP only once joined
static void ap_test_save(EGHttpResponse& res) {
  (void)corium::Wifi::testJoin();   // held just now, ap mode: can't fail
  s_ap_test = true;
  char ssid[6 * EGPORTAL_SSID_MAX + 1], host[6 * CORIUM_HOST_MAX + 1];
  egesc_html(ssid, sizeof(ssid), corium::Wifi::testSsid());
  egesc_html(host, sizeof(host), corium::DeviceInfo::hostname());
  corium::WebPortal::beginPage(res, F("Saving WiFi"));
  res.sendKV(F("<p id=jt data-u=/wifijoin data-k=1 data-b=/network data-a='"), host, F("' data-h='"));
  res.sendKV(nullptr, host, F(".local'>Testing the connection to <b>"));
  res.sendKV(nullptr, ssid, F("</b>. This can take up to 30 seconds.</p><script>"));
  res.sendChunk(FPSTR(EGPortal::JOIN_JS));
  res.sendChunk(F("</script>"));
  corium::WebPortal::endPage(res);
}

// {"j":code,"s":tested,"ip":the STA's}, hotspot mode's page polls it
void corium::WebPortal::hWifiJoin(EGHttpRequest& req, EGHttpResponse& res, void*) {
  char ip[CORIUM_IPV4_MAX + 1];
  corium::Wifi::formatIP(ip, sizeof(ip), corium::Wifi::localIP());
  char out[6 * WIFI_SSID_MAX + 48];   // the ssid json-escaped
  EGJson js(out, sizeof(out));
  js.map_begin();
  js.uint(PSTR("j"), corium::Wifi::testState());
  js.str(PSTR("s"), corium::Wifi::testSsid());
  js.str(PSTR("ip"), ip);
  js.map_end();
  if (js.overflow || js.length() >= sizeof(out)) {
    res.send(500, FPSTR(HTTP_CT_PLAIN), F("status too long"));
    return;
  }
  out[js.length()] = '\0';
  res.send(200, FPSTR(HTTP_CT_JSON), out);
}

void corium::WebPortal::hWifiSave(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // write all four, keeps values on toggle
  bool static_on = (req.arg(FPSTR(corium::K_STATIC_IP)) != nullptr);
  char ip[CORIUM_IPV4_MAX + 1], gw[CORIUM_IPV4_MAX + 1];
  char sn[CORIUM_IPV4_MAX + 1], dns[CORIUM_IPV4_MAX + 1];
  if (!take_ip(req, FPSTR(corium::K_IP),  ip)
      || !take_ip(req, FPSTR(corium::K_GW),  gw)
      || !take_ip(req, FPSTR(corium::K_SN),  sn)
      || !take_ip(req, FPSTR(corium::K_DNS), dns)) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("Bad network config: not an IP address\nClick Back to fix it.\n"));
    return;
  }

  if (static_on && static_refused(res, ip, gw, sn)) return;

  const char* ssid = req.arg(F("ssid"));
  bool has_ssid = (ssid && *ssid);
  HoldResult held = has_ssid ? wifi_hold_args(req, ssid) : HELD;
  if (!wifi_held_ok(res, held)) return;

  bool put = put_net(static_on, has_ssid, ip, gw, sn, dns);
  if (!corium::Prefs::commit() || !put) {
    if (has_ssid) corium::Wifi::dropHeld();
    not_saved_page(res, F("Saving WiFi"));
    return;
  }
  wifi_saved(res, has_ssid);
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
}else fl(x.responseText);
};
function fl(t){B.disabled=false;S.innerHTML='<b style="color:#c0392b"></b>';S.firstChild.textContent=t?'Upload failed: '+t:'Upload error (connection lost?)'}
x.onerror=function(){
fetch('/update/progress',{cache:'no-store'}).then(r=>r.json()).then(j=>fl(j.err)).catch(()=>fl());
};
x.send(f);
});
})();
</script>
)HTML";

// BUILD_ENV alone matches other variants
// keep it used, gc-sections drops it
static const char EG_ENV_TAG[] PROGMEM = "<EGENV:" BUILD_ENV ":ENV>";

void corium::WebPortal::hUpdateForm(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Update"));
  char dev[140];
  send_fmt(res, dev, snprintf_P(dev, sizeof(dev),
    PSTR("<script>window._dev={env:\"" BUILD_ENV "\",tag:\"%s\",rs:%u};</script>"),
    EG_ENV_TAG, (unsigned)FLASH_COUNTDOWN_SECS), sizeof(dev));
  res.sendChunk(FPSTR(UPDATE_FORM_BODY));
  corium::WebPortal::endPage(res);
}

// one upload at a time
static struct {
  size_t expected = 0;
  size_t delivered = 0;
  bool   write_error = false;
  uint8_t head[8];         // held until entry addr checked
  uint8_t head_n = 0;
  bool    started = false; // OTA_FAILED owed
  PGM_P   why = nullptr;   // for the page
} s_ota;

static void ota_fail(PGM_P why) {
  s_ota.write_error = true;
  if (s_ota.why) return;
  s_ota.why = why;
#ifdef ESP8266
  corium::Wifi::applySleep();   // Update.begin set none
#endif
}

// too big is SPACE on the 8266, SIZE on the esp32
static PGM_P begin_why() {
  uint8_t e = Update.getError();
  if (e == UPDATE_ERROR_SPACE || e == UPDATE_ERROR_SIZE) return PSTR("no room");
#ifdef ESP8266
  if (e == UPDATE_ERROR_OOM) return PSTR("out of memory");
#else
  if (e == UPDATE_ERROR_OK) return PSTR("out of memory");   // its buffer malloc sets none
#endif
  return PSTR("begin failed");
}

static void ota_begin(size_t expected) {
  s_ota.expected    = expected;
  s_ota.delivered   = 0;
  s_ota.write_error = false;
  s_ota.head_n      = 0;
  s_ota.started     = false;
  s_ota.why         = nullptr;
  // a save's restart would cut it
  if (corium::Lifecycle::restartPending()) {
    ota_fail(PSTR("restarting, try again"));
    return;
  }
  // no length given: free space less one 4 KB sector, sector aligned
  uint32_t room = (corium::DeviceInfo::freeSketch() - 0x1000) & 0xFFFFF000;
  if (!Update.begin(s_ota.expected ? s_ota.expected : room)) {
    corium::Log::console(PSTR("OTA: Update.begin failed (size=%u, free=%u)"),
                 (unsigned)s_ota.expected, (unsigned)room);
    ota_fail(begin_why());
  } else {
    corium::Log::console(PSTR("OTA: starting (%u bytes)"), (unsigned)s_ota.expected);
    s_ota.started = true;
    corium::Lifecycle::send(corium::Lifecycle::OTA_START);
  }
}

static void ota_failed() {
  if (!s_ota.started) return;
  s_ota.started = false;
  corium::Lifecycle::send(corium::Lifecycle::OTA_FAILED);
}

// a bin's entry point must sit in this chip's IRAM
#ifdef ESP8266
// esp8266 IRAM
static constexpr uint32_t ENTRY_LO = 0x40100000UL, ENTRY_HI = 0x40200000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// S3 IRAM
static constexpr uint32_t ENTRY_LO = 0x40370000UL, ENTRY_HI = 0x403E0000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
static constexpr uint32_t ENTRY_LO = 0x40020000UL, ENTRY_HI = 0x40050000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
static constexpr uint32_t ENTRY_LO = 0x40380000UL, ENTRY_HI = 0x403E0000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
static constexpr uint32_t ENTRY_LO = 0x40800000UL, ENTRY_HI = 0x40880000UL;
#else
// esp32 IRAM
static constexpr uint32_t ENTRY_LO = 0x40080000UL, ENTRY_HI = 0x400A0000UL;
#endif

static bool ota_entry_ok(uint32_t entry) {
  return entry >= ENTRY_LO && entry < ENTRY_HI;
}

static bool ota_write(const uint8_t* p, size_t len) {
  size_t wrote = Update.write((uint8_t*)p, len);
  if (wrote != len) {
    corium::Log::console(PSTR("OTA: short write %u/%u err=%u"),
                 (unsigned)wrote, (unsigned)len, Update.getError());
    ota_fail(PSTR("write failed"));
    return false;
  }
  s_ota.delivered += wrote;
  return true;
}

static void ota_data(const char* chunk, size_t len) {
  if (s_ota.write_error || !Update.isRunning()) return;   // already failed
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
    uint32_t entry = rdLe32(h + 4);   // image header bytes 4-7
    if (!ota_entry_ok(entry)) {
      corium::Log::console(PSTR("OTA: REFUSING wrong-platform bin (entry=0x%08x)"),
                   (unsigned)entry);
      Update.end(false);
      ota_fail(PSTR("wrong platform"));
      return;
    }
    if (!ota_write(h, sizeof(s_ota.head))) return;
  }
  if (len) ota_write((const uint8_t*)chunk, len);
}

static void ota_end() {
  // Update.end() is strict, needs full size
  if (s_ota.write_error || !Update.isRunning()) {
    corium::Log::console(PSTR("OTA: aborting on prior error"));
    if (Update.isRunning()) Update.end(false);  // discard
    ota_fail(PSTR("not running"));
    return;
  }
  if (s_ota.delivered != s_ota.expected) {
    corium::Log::console(PSTR("OTA: byte-count mismatch %u != %u"),
                 (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
    Update.end(false);
    ota_fail(PSTR("size mismatch"));
    return;
  }
  if (Update.end()) {
    corium::Log::console(PSTR("OTA: success (%u bytes), restarting"),
                 (unsigned)s_ota.delivered);
    // here too, reply may never go out
    corium::WebPortal::requestRestart();
  } else {
    corium::Log::console(PSTR("OTA: end failed err=%u"), Update.getError());
    ota_fail(PSTR("image check failed"));
  }
}

static void ota_abort() {
  if (Update.isRunning()) Update.end(false);
  ota_fail(PSTR("connection lost"));
  corium::Log::console(PSTR("OTA: aborted at %u/%u bytes"),
               (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
}

bool corium::WebPortal::hUpdateBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* chunk, size_t len, void*) {
  // main loop, flash write ok
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: ota_begin(req.bodyLen()); break;
    case EGHttpServer::BODY_DATA:  ota_data(chunk, len);     break;
    case EGHttpServer::BODY_END:   ota_end();                break;
    case EGHttpServer::BODY_ABORT: ota_abort();              break;
  }
  if (s_ota.write_error) ota_failed();
  return !s_ota.write_error;
}

void corium::WebPortal::hUpdateDone(EGHttpRequest& req, EGHttpResponse& res, void*) {
  if (s_ota.write_error || Update.hasError()) {
    char msg[96];
    snprintf_P(msg, sizeof(msg),
               PSTR("Update failed: %s, %u/%u bytes, err=%u"),
               s_ota.why ? s_ota.why : PSTR("error"),
               (unsigned)s_ota.delivered, (unsigned)s_ota.expected,
               (unsigned)Update.getError());
    res.send(500, FPSTR(HTTP_CT_PLAIN), msg);
    return;
  }
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
  corium::WebPortal::requestRestart();
}

// device-side count, 0 when idle; err after a refused upload
void corium::WebPortal::hUpdateProgress(EGHttpRequest& req, EGHttpResponse& res, void*) {
  char buf[80];
  snprintf_P(buf, sizeof(buf), PSTR("{\"done\":%u,\"total\":%u,\"err\":\"%s\"}"),
             (unsigned)s_ota.delivered, (unsigned)(s_ota.started ? s_ota.expected : 0),
             s_ota.why ? s_ota.why : PSTR(""));
  res.addHeader(FPSTR(HTTP_H_CACHE), F("no-store"));
  res.send(200, FPSTR(HTTP_CT_JSON), buf);
}

// /param

// a literal from flash, dropped if it won't fit
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

static constexpr size_t BODY_MAX = 8192;   // largest body held, it's heap

static bool body_size_ok(size_t cap) {
  return cap != 0 && cap <= BODY_MAX;
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

// path nullptr = quiet
static void body_begin(BodyBuf& b, size_t cap, PGM_P path) {
  body_free(b);
  if (!body_size_ok(cap)) {
    if (path) corium::Log::console(PSTR("%s: body size %u out of range"), path, (unsigned)cap);
    return;
  }
  if (!body_alloc(b, cap) && path) {
    corium::Log::console(PSTR("%s: malloc(%u) failed"), path, (unsigned)(cap + 1));
  }
}

// no buffer, the rest isn't read
static bool body_event(BodyBuf& b, EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                       const char* chunk, size_t len, PGM_P path) {
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: body_begin(b, req.bodyLen(), path); break;
    case EGHttpServer::BODY_DATA:  body_append(b, chunk, len);         break;
    case EGHttpServer::BODY_END:   body_end(b);                        break;
    case EGHttpServer::BODY_ABORT: body_free(b);                       break;
  }
  return b.buf != nullptr;
}

// freed in hParam
static BodyBuf s_param_body;

bool corium::WebPortal::hParamBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                            const char* chunk, size_t len, void*) {
  return body_event(s_param_body, req, ev, chunk, len, P_PARAM);
}

// first field a save refused, and first saved with a warning, shown above the form
struct Refusal {
  char name[2 * CORIUM_PREF_ID_MAX + 2];   // mid.id
  char why[corium::PinSafety::WHY_MAX];       // copied, the next put reuses "in use"
  char note_name[2 * CORIUM_PREF_ID_MAX + 2];
  const char* note;                   // PROGMEM
};

static void param_put(const char* mid, const char* id, const char* name,
                      const char* v, Refusal& r) {
  if (corium::Prefs::put(mid, id, v)) {
    if (r.note || !corium::Prefs::warned()) return;
    strlcpy(r.note_name, name, sizeof(r.note_name));
    r.note = corium::Prefs::warned();
    return;
  }
  if (r.why[0]) return;
  strncpy(r.name, name, sizeof(r.name) - 1);
  r.name[sizeof(r.name) - 1] = '\0';
  strncpy_P(r.why, corium::Prefs::refused() ? corium::Prefs::refused() : PSTR("not valid"), sizeof(r.why) - 1);
}

static void param_save_pref(const char* mid, const corium::Pref& p,
                            const char* body, size_t len, Refusal& r) {
  if (p.flags & (corium::P_HIDDEN | corium::P_READONLY)) return;
  if (p.type == corium::P_LABEL || p.type == corium::P_HEADER) return;
  char name[2 * CORIUM_PREF_ID_MAX + 2];   // mid.id
  char id[CORIUM_PREF_ID_MAX + 1];
  char field[CORIUM_PREFS_MAX_VALUE + 1];
  flashToRam(p.id, id, sizeof(id));
  snprintf_P(name, sizeof(name), PSTR("%s.%s"), mid, id);

  const char* v = EGHttpRequest::decodeArg(body, len, name, field, sizeof(field));
  if (p.type == corium::P_BOOL) {
    v = v ? "1" : "0";
  } else if (!v) {
    return;
  } else if (p.flags & corium::P_SENSITIVE) {
    if (strcmp_P(v, PSTR("__CLEAR__")) == 0) v = "";
    else if (v[0] == '\0') return;
  }
  param_put(mid, id, name, v, r);
}

// false = nothing saved
static bool param_save_body(uint8_t tab, Refusal& r) {
  const char* body = s_param_body.buf;
  size_t      len  = s_param_body.len;
  bool        ok   = s_param_body.ok && body;

  if (ok) {
    for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
      const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
      if (!g) continue;
      // this tab only, else other bools clear
      if (g->category.get() != tab) continue;
      // not on the page, a missing bool would save as off
      corium::Module* m = corium::Prefs::moduleAt(gi);
      if (m && m->displayOrder() == 0) continue;
      for (size_t j = 0; j < g->count.get(); j++) {
        const corium::Pref p = g->prefs.row(j);
        param_save_pref(g->moduleId, p, body, len, r);
      }
    }
  }
  body_free(s_param_body);
  return ok;
}

#ifndef APP_PARAM_TABS
#define APP_PARAM_TABS "System", "Input", "Output", "Upload", "Backup"   // app sets it
#endif

static constexpr uint8_t TABS = corium::P_CAT_BACKUP + 1;

static void param_render_tabs(EGHttpResponse& res, uint8_t tab) {
  static const char TAB_NAMES[TABS][7] PROGMEM = {APP_PARAM_TABS};
  res.sendChunk(F("<nav class=tabs>"));
  char nav[96];
  for (uint8_t t = 0; t < TABS; t++) {
    send_fmt(res, nav, snprintf_P(nav, sizeof(nav),
      PSTR("<a href='/param?t=%u'%s>%s</a>"),
      (unsigned)t, t == tab ? PSTR(" class=ac") : PSTR(""), TAB_NAMES[t]), sizeof(nav));
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
  corium::Module* m = corium::Prefs::moduleAt(gi);
  if (m && !m->hwPresent()) return false;
  const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
  return !g || corium::Prefs::groupEnabled(g);
}

static uint8_t param_group_order(uint8_t gi) {
  corium::Module* m = corium::Prefs::moduleAt(gi);
  return m ? m->displayOrder() : 50;
}

// on groups first, then displayOrder; insertion sort, keeps ties in order
static void param_sort_groups(uint8_t* order, uint8_t gc) {
  static constexpr uint16_t OFF_LAST = 0x100;
  uint16_t key[CORIUM_MAX_MODULES];
  for (uint8_t i = 0; i < gc; i++) {
    order[i] = i;
    key[i] = (param_group_on(i) ? 0 : OFF_LAST) | param_group_order(i);
  }
  for (uint8_t i = 1; i < gc; i++) {
    uint8_t cur = order[i];
    uint8_t j = i;
    while (j > 0 && key[order[j - 1]] > key[cur]) {
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
  const corium::PrefShow* show;
  char        buf[512];
  char        id[CORIUM_PREF_ID_MAX + 1], lbl[80], help[120], pat[CORIUM_PREF_PATTERN_MAX + 1];
  char        attr[2 * CORIUM_PREF_ID_MAX + 32];   // data-w='mid.key' data-m=N
};

static void param_appendf(ParamCtx& c, PGM_P fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf_P(c.buf + c.pos, sizeof(c.buf) - c.pos, fmt, ap);
  va_end(ap);
  advancePos(c.pos, n, sizeof(c.buf));
}

// one chunk from the start of buf
static void param_sendf(ParamCtx& c, PGM_P fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf_P(c.buf, sizeof(c.buf), fmt, ap);
  va_end(ap);
  send_fmt(*c.res, c.buf, n, sizeof(c.buf));
}

// for the form script, "" = always shown
static const char* show_attr(ParamCtx& c, const char* id) {
  corium::PrefShow r;
  c.attr[0] = '\0';
  if (corium::Prefs::showRule(c.show, id, &r)) {
    snprintf_P(c.attr, sizeof(c.attr), PSTR(" data-w='%s' data-m=%u"),
               r.when, (unsigned)r.values);
  }
  return c.attr;
}

static void param_group_header(ParamCtx& c, const corium::PrefGroup* g, corium::Module* mod) {
  bool has_toggle = (g->enableKey != nullptr);
  bool grp_on = corium::Prefs::groupEnabled(g);
  bool hw_absent = (mod && !mod->hwPresent());
  bool off = hw_absent;
  PGM_P badge = PSTR("");
  if (hw_absent) {
    badge = PSTR(" <span class='gb off'>not detected</span>");
  } else if (has_toggle) {
    off = !grp_on;
    badge = grp_on ? PSTR(" <span class='gb on'>on</span>") : PSTR(" <span class='gb off'>off</span>");
  }
  param_sendf(c, PSTR("<details class='bx%s'%s><summary>%s%s</summary>"),
              off ? PSTR(" off") : PSTR(""), show_attr(c, nullptr), g->label, badge);
}

static void param_emit_bool(ParamCtx& c, const char* lbl, bool inline_bool) {
  PGM_P checked = corium::Prefs::getBool(c.mid, c.id) ? W_CHECKED : PSTR("");
  if (inline_bool) {
    param_appendf(c,
      PSTR("<label class='il' title='%s'><input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s>%s</label>"),
      c.help, c.mid, c.id, c.mid, c.id, checked, c.disabled, lbl);
  } else {
    param_appendf(c,
      PSTR("<input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s><br>"),
      c.mid, c.id, c.mid, c.id, checked, c.disabled);
  }
}

// sent per option, a long list can't overflow buf
static void param_emit_options(ParamCtx& c, const corium::Pref& p) {
  int32_t cur_idx = 0, def_idx = -1;
  parseI32(c.cur, &cur_idx);
  parseI32(p.defaultVal, &def_idx);
  const char* opt = c.pat;
  long idx = p.minI;
  while (*opt) {
    const char* sep = strchr(opt, '|');
    size_t olen = sep ? (size_t)(sep - opt) : strlen(opt);
    // blank holds a number this build hasn't got
    if (olen) {
      param_sendf(c, PSTR("<option value=%ld%s>%.*s%s</option>"),
                  idx, (idx == cur_idx) ? PSTR(" selected") : PSTR(""),
                  (int)olen, opt, (idx == def_idx) ? PSTR(" (default)") : PSTR(""));
    }
    if (!sep) break;
    opt = sep + 1;
    idx++;
  }
}

static void param_emit_enum(ParamCtx& c, const corium::Pref& p) {
  if (c.pos) { c.res->sendChunk(c.buf, c.pos); c.pos = 0; }
  param_sendf(c, PSTR("<select id='%s.%s' name='%s.%s'%s>"),
              c.mid, c.id, c.mid, c.id, c.disabled);
  param_emit_options(c, p);
  param_sendf(c, PSTR("</select><br>"));
  c.pos = 0;
}

static PGM_P param_input_type(const corium::Pref& p, bool slider) {
  if (slider) return PSTR("range");
  if (p.type == corium::P_INT || p.type == corium::P_UINT || p.type == corium::P_FLOAT) return PSTR("number");
  if (p.type == corium::P_STRING && (p.flags & corium::P_SENSITIVE)) return PSTR("password");
  if (p.type == corium::P_STRING && (p.flags & corium::P_TIME))      return PSTR("time");
  return PSTR("text");
}

// html grows 6x, a cut value would be saved cut
static constexpr size_t ESC_PIECE = sizeof(ParamCtx::buf) / 6;

static void param_send_escaped(ParamCtx& c, const char* s) {
  char piece[ESC_PIECE + 1];
  for (size_t left = strlen(s); left; ) {
    size_t k = left < ESC_PIECE ? left : ESC_PIECE;
    // whole chars, a cut one comes back as two U+FFFD
    while (k < left && k > 1 && (s[k] & 0xC0) == 0x80) k--;
    memcpy(piece, s, k);
    piece[k] = '\0';
    c.res->sendChunk(c.buf, egesc_html(c.buf, sizeof(c.buf), piece));
    s += k;
    left -= k;
  }
}

static void param_emit_value(ParamCtx& c, const corium::Pref& p, bool slider) {
  if (p.flags & corium::P_SENSITIVE) {
    PGM_P ph = c.cur[0] ? PSTR("(unchanged \xE2\x80\x94 leave blank)") : PSTR("(not set)");
    param_appendf(c, PSTR(" value='' placeholder='%s'"), ph);
    return;
  }
  if (c.pos) { c.res->sendChunk(c.buf, c.pos); c.pos = 0; }
  c.res->sendChunk(F(" value='"));
  param_send_escaped(c, c.cur);
  c.buf[c.pos++] = '\'';
  if (!slider && p.defaultVal && p.defaultVal[0]
      && !(p.flags & corium::P_TIME)) {
    param_appendf(c, PSTR(" placeholder='%s'"), p.defaultVal);
  }
}

static void param_emit_limits(ParamCtx& c, const corium::Pref& p) {
  if (p.type == corium::P_STRING) {
    if (p.maxLen > 0) param_appendf(c, PSTR(" maxlength='%u'"), (unsigned)p.maxLen);
    if (p.minI > 0) param_appendf(c, PSTR(" minlength='%ld'"), (long)p.minI);
    if (p.pattern && c.pat[0]) param_appendf(c, PSTR(" pattern='%s'"), c.pat);
    return;
  }
  if ((p.type == corium::P_INT || p.type == corium::P_UINT || p.type == corium::P_FLOAT) && p.minI != p.maxI) {
    param_appendf(c, PSTR(" min=%ld max=%ld"), (long)p.minI, (long)p.maxI);
  }
}

static void param_emit_input_end(ParamCtx& c, const corium::Pref& p, bool slider) {
  if (p.type == corium::P_FLOAT) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " step=any");
  if (!slider && (p.type == corium::P_INT || p.type == corium::P_UINT)) {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " inputmode=numeric");
  } else if (p.type == corium::P_FLOAT) {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " inputmode=decimal");
  }
  if (p.flags & corium::P_REQUIRED) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " required");
  if (p.flags & corium::P_READONLY) APPEND_LIT(c.buf, c.pos, sizeof(c.buf), " readonly");
  if (slider) {
    param_appendf(c,
      PSTR(" oninput='this.nextElementSibling.value=this.value'><output>%s</output><br>"), c.cur);
  } else {
    APPEND_LIT(c.buf, c.pos, sizeof(c.buf), "><br>");
  }
}

static void param_emit_input(ParamCtx& c, const corium::Pref& p) {
  const bool slider = (p.type == corium::P_INT || p.type == corium::P_UINT)
                   && (p.flags & corium::P_SLIDER)
                   && p.minI != p.maxI;
  param_appendf(c, PSTR("<input type=%s id='%s.%s' name='%s.%s'"),
                param_input_type(p, slider), c.mid, c.id, c.mid, c.id);
  param_emit_value(c, p, slider);
  param_emit_limits(c, p);
  param_emit_input_end(c, p, slider);
}

static void param_emit_help(ParamCtx& c, const corium::Pref& p, bool inline_bool) {
  if (!inline_bool && p.help && c.help[0]) {
    param_appendf(c, PSTR("<small>%s</small>"), c.help);
  }
  if ((p.flags & corium::P_SENSITIVE) && c.cur[0]) {
    param_appendf(c,
      PSTR("<button type=button class='danger btn-sm' style='margin-top:.2em' "
           "onclick=\"if(confirm('Clear stored value?')){byID('%s.%s').value='__CLEAR__';this.form.submit();}\""
           ">Clear</button>"),
      c.mid, c.id);
  }
  c.res->sendChunk(c.buf, c.pos);
}

static void param_load_text(ParamCtx& c, const corium::Pref& p) {
  flashToRam(p.id,      c.id,   sizeof(c.id));
  flashToRam(p.label,   c.lbl,  sizeof(c.lbl));
  flashToRam(p.help,    c.help, sizeof(c.help));
  flashToRam(p.pattern, c.pat,  sizeof(c.pat));
}

static void param_emit_heading(ParamCtx& c, const corium::Pref& p) {
  param_sendf(c, p.type == corium::P_LABEL ? PSTR("<label>%s</label>") : PSTR("<h4>%s</h4>"), c.lbl);
}

static void param_emit_label(ParamCtx& c, const char* lbl) {
  int n = snprintf_P(c.buf, sizeof(c.buf), PSTR("<label for='%s.%s'>%s</label>"), c.mid, c.id, lbl);
  if (n > 0 && (size_t)n < sizeof(c.buf)) c.pos = (size_t)n;
}

static void param_emit_row(ParamCtx& c, const corium::Pref& p);

// a row with a show rule sits in its own div
static void param_emit_field(ParamCtx& c, const corium::Pref& p) {
  param_load_text(c, p);
  bool wrap = show_attr(c, c.id)[0];
  if (wrap) {
    c.res->sendChunk(F("<div"));
    c.res->sendChunk(c.attr);
    c.res->sendChunk(F(">"));
  }
  param_emit_row(c, p);
  if (wrap) c.res->sendChunk(F("</div>"));
}

static void param_emit_row(ParamCtx& c, const corium::Pref& p) {
  if (p.type == corium::P_LABEL || p.type == corium::P_HEADER) {
    param_emit_heading(c, p);
    return;
  }

  c.cur = corium::Prefs::getString(c.mid, c.id);
  const char* lbl = p.label ? c.lbl : c.id;
  c.disabled = (p.flags & corium::P_READONLY) ? PSTR(" disabled") : PSTR("");
  bool inline_bool = (p.type == corium::P_BOOL) && (p.flags & corium::P_INLINE);
  c.pos = 0;
  if (!inline_bool) param_emit_label(c, lbl);

  if (p.type == corium::P_BOOL)      param_emit_bool(c, lbl, inline_bool);
  else if (p.type == corium::P_ENUM) param_emit_enum(c, p);
  else                         param_emit_input(c, p);
  param_emit_help(c, p, inline_bool);
}

static void param_render_group(ParamCtx& c, const corium::PrefGroup* g, corium::Module* mod) {
  param_group_header(c, g, mod);
  c.mid = g->moduleId;

  for (size_t j = 0; j < g->count.get(); j++) {
    const corium::Pref p = g->prefs.row(j);
    if (p.flags & (corium::P_HIDDEN | corium::P_ADVANCED)) continue;
    param_emit_field(c, p);
  }
  bool adv_open = false;
  for (size_t j = 0; j < g->count.get(); j++) {
    const corium::Pref p = g->prefs.row(j);
    if ((p.flags & corium::P_HIDDEN) || !(p.flags & corium::P_ADVANCED)) continue;
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
  uint8_t gc = (uint8_t)corium::Prefs::groupCount();
  uint8_t order[CORIUM_MAX_MODULES];
  if (gc > sizeof(order)) gc = sizeof(order);
  param_sort_groups(order, gc);

  for (uint8_t gi = 0; gi < gc; gi++) {
    corium::Module* mod = corium::Prefs::moduleAt(order[gi]);
    if (mod && mod->displayOrder() == 0) continue;   // has its own page
    const corium::PrefGroup* g = corium::Prefs::groupAt(order[gi]);
    if (!g || g->count.get() == 0) continue;
    if (g->category.get() != tab) continue;
    c.show = mod ? mod->prefsShow() : nullptr;
    param_render_group(c, g, mod);
  }
}

static void param_render_form(EGHttpResponse& res, uint8_t tab) {
  char form_open[48];
  send_fmt(res, form_open, snprintf_P(form_open, sizeof(form_open),
    PSTR("<form method=POST action='/param?t=%u'>"), (unsigned)tab), sizeof(form_open));
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
  if (parseI32(req.arg(F("t")), &t) && t >= 0 && t < TABS) return (uint8_t)t;
  return (uint8_t)corium::P_CAT_SYSTEM;
}

static const char RESTART_APPLY[] PROGMEM = " <a href=/restart>Restart</a> to apply.";

static PGM_P link_once(bool& link, PGM_P text) {
  if (!link) return PSTR("");
  link = false;
  return text;
}

// what a post did, above the form; frozen = a group left out, the rest saved
static void param_says(EGHttpResponse& res, bool saved, bool wrote, bool restart,
                       const Refusal& refused, const char* frozen) {
  if (wrote) res.sendChunk(F(
    "<div class='a ak'>Saved.</div>"));
  else if (saved) corium::WebPortal::sendNotSaved(res);
  char buf[160];
  bool link = restart;
  if (refused.why[0]) {
    snprintf_P(buf, sizeof(buf), PSTR("<div class='a ae'>Not saved: %s, %s.%s</div>"),
               refused.name, refused.why,
               link_once(link, PSTR(" <a href=/restart>Restart</a> for the rest.")));
    res.sendChunk(buf);
  }
  if (refused.note) {
    snprintf_P(buf, sizeof(buf), PSTR("<div class='a aw'>%s, %s.%s</div>"),
               refused.note_name, refused.note, link_once(link, RESTART_APPLY));
    res.sendChunk(buf);
  }
  if (frozen) {
    snprintf_P(buf, sizeof(buf), PSTR("<div class='a aw'>%s.%s</div>"), frozen,
               link_once(link, RESTART_APPLY));
    res.sendChunk(buf);
  }
  if (!saved) res.sendChunk(F(
    "<div class='a ae'>Not saved: the form was too large or memory was short.</div>"));
}

void corium::WebPortal::hParam(EGHttpRequest& req, EGHttpResponse& res, void*) {
  uint8_t tab = param_tab(req);
  bool posted = (req.method() == EGHttpRequest::POST);
  Refusal refused{};
  bool saved = posted && param_save_body(tab, refused);
  bool wrote = saved && corium::Prefs::commit();
  // every tab row is put, a frozen group on it mustn't read as not saved
  char why[CORIUM_PREFS_FROZEN_MAX];
  const char* frozen = saved && !wrote && corium::Prefs::frozen(why, sizeof(why)) ? why : nullptr;
  if (frozen) wrote = true;
  // stay, so the refusal or note shows
  bool restart = wrote && corium::Prefs::restartPending();
  if (restart && !refused.why[0] && !refused.note && !frozen) {
    res.redirect(FPSTR(P_RESTART));
    return;
  }

  corium::WebPortal::beginPage(res, F("Config"));
  if (posted) param_says(res, saved, wrote, restart, refused, frozen);
  param_render_tabs(res, tab);

  // Backup is the import form only
  if (tab == (uint8_t)corium::P_CAT_BACKUP) param_render_backup(res);
  else                                param_render_form(res, tab);
  corium::WebPortal::endPage(res);
}

// crc32 (reflected), a bit at a time; mask is all ones when the low bit is set
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

static constexpr uint32_t EXPORT_OBF_SEED = CORIUM_EXPORT_SEED;
// magic, version, groups, crc
static constexpr size_t  BLOB_MAGIC = 3, BLOB_HEAD = BLOB_MAGIC + 1, BLOB_CRC = 4;
static constexpr uint8_t BLOB_VER = 1;

// base64 straight into the response, crc over the bytes as sent
struct B64Sink {
  EGHttpResponse* res;
  uint8_t  tri[3];
  uint8_t  tri_n;
  char     out[256];
  size_t   out_len;
  uint32_t crc;
  uint32_t obf_state;
  bool     obf_on;
};

static void b64_init(B64Sink& s, EGHttpResponse& r) {
  s.res = &r; s.tri_n = 0; s.out_len = 0; s.crc = 0;
  s.obf_state = EXPORT_OBF_SEED; s.obf_on = false;
}

// xorshift keystream byte, keeps the blob unreadable at a glance, not crypto
static inline uint8_t obf_step(uint32_t* st) {
  uint32_t x = xorshift32(*st);
  *st = x;
  return (uint8_t)(x & 0xFF);
}
static void b64_flush_out(B64Sink& s) {
  if (s.out_len) { s.res->sendChunk(s.out, s.out_len); s.out_len = 0; }
}
static void b64_emit_quartet(B64Sink& s, uint8_t in_len) {
  if (s.out_len + 4 > sizeof(s.out)) b64_flush_out(s);
  // writes a NUL too
  unsigned char quad[5];
  encode_base64(s.tri, in_len, quad);
  memcpy(s.out + s.out_len, quad, 4);
  s.out_len += 4;
  s.tri_n = 0;
}
static void b64_put(B64Sink& s, uint8_t b) {
  s.tri[s.tri_n++] = b;
  if (s.tri_n == 3) b64_emit_quartet(s, 3);
}
static void b64_write(B64Sink& s, const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = p[i];
    if (s.obf_on) b ^= obf_step(&s.obf_state);
    s.crc = crc32_update(s.crc, &b, 1);
    b64_put(s, b);
  }
}
static void b64_write_u8(B64Sink& s, uint8_t v) { b64_write(s, &v, 1); }
static void b64_finish(B64Sink& s) {
  if (s.tri_n > 0) b64_emit_quartet(s, s.tri_n);
  b64_flush_out(s);
}

// id gets the key, val_out its value
static bool is_exportable_pref(const corium::PrefGroup* g, const corium::Pref& p,
                               char (&id)[CORIUM_PREF_ID_MAX + 1], const char*& val_out) {
  if (p.type == corium::P_LABEL || p.type == corium::P_HEADER) return false;
  if (p.flags & corium::P_LOCAL) return false;
  flashToRam(p.id, id, sizeof(id));
  const char* cur = corium::Prefs::getString(g->moduleId, id);
  if (!cur) cur = "";
  if (p.defaultVal && strcmp(cur, p.defaultVal) == 0) return false;
  val_out = cur;
  return true;
}

static uint8_t group_export_count(const corium::PrefGroup* g) {
  uint8_t n = 0;
  for (size_t j = 0; j < g->count.get(); j++) {
    char id[CORIUM_PREF_ID_MAX + 1];
    const char* cur;
    if (is_exportable_pref(g, g->prefs.row(j), id, cur)) n++;
  }
  return n;
}

static void export_write_group(B64Sink& sink, const corium::PrefGroup* g, uint8_t n) {
  size_t mid_len = strlen(g->moduleId);
  if (mid_len > 255) return;
  b64_write_u8(sink, (uint8_t)mid_len);
  b64_write(sink, g->moduleId, mid_len);
  b64_write_u8(sink, n);

  for (size_t j = 0; j < g->count.get(); j++) {
    const corium::Pref p = g->prefs.row(j);
    char id[CORIUM_PREF_ID_MAX + 1];
    const char* cur;
    if (!is_exportable_pref(g, p, id, cur)) continue;
    size_t klen = strlen(id);
    size_t vlen = strlen(cur);
    if (klen > 255 || vlen > 65535) continue;
    b64_write_u8(sink, (uint8_t)klen);
    b64_write(sink, id, klen);
    uint8_t vb[2];
    wrBe16(vb, (uint16_t)vlen);
    b64_write(sink, vb, 2);
    b64_write(sink, cur, vlen);
  }
}

static void export_stream(B64Sink& sink) {
  uint8_t groups = 0;
  for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
    const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
    if (!g || g->count.get() == 0) continue;
    if (group_export_count(g)) groups++;
  }

  const uint8_t MAGIC[BLOB_MAGIC] = {CORIUM_EXPORT_MAGIC};
  b64_write(sink, MAGIC, BLOB_MAGIC);
  b64_write_u8(sink, BLOB_VER);
  // magic and version stay clear, the rest is xored
  sink.obf_on = true;
  b64_write_u8(sink, groups);

  for (size_t gi = 0; gi < corium::Prefs::groupCount(); gi++) {
    const corium::PrefGroup* g = corium::Prefs::groupAt(gi);
    if (!g || g->count.get() == 0) continue;
    uint8_t n = group_export_count(g);
    if (n == 0) continue;
    export_write_group(sink, g, n);
  }
}

static void deobfuscate_payload(uint8_t* buf, size_t len) {
  uint32_t st = EXPORT_OBF_SEED;
  for (size_t i = 0; i < len; i++) buf[i] ^= obf_step(&st);
}

static const char* check_import(uint8_t* buf, size_t len) {
  if (len < BLOB_HEAD + 1 + BLOB_CRC) return PSTR("too short");   // no groups
  // the magic is blobStart's 4 base64 chars
  if (buf[BLOB_MAGIC] != BLOB_VER) return PSTR("bad format");
  size_t payload_len = len - BLOB_CRC;
  // crc is over the xored bytes, check before undoing them
  uint32_t expected = rdBe32(buf + payload_len);
  uint32_t actual = crc32_update(0, buf, payload_len) ^ EXPORT_OBF_SEED;
  if (expected != actual) return PSTR("checksum mismatch");

  deobfuscate_payload(buf + BLOB_HEAD, payload_len - BLOB_HEAD);
  return corium::ImportText::blobError(buf + BLOB_HEAD, payload_len - BLOB_HEAD);
}

static bool import_local(const char* mid, const char* key) {
  corium::Pref p;
  return corium::Prefs::findPref(mid, key, &p) && (p.flags & corium::P_LOCAL);
}

// put refuses unknown keys, forwards moved ones
static bool import_put(const char* mid, const char* key, const char* value) {
  return !import_local(mid, key) && corium::Prefs::put(mid, key, value);
}

// a backup's values were saved before, a newer shortest rule mustn't drop them
static bool import_kept(const char* mid, const char* key, const char* value) {
  return !import_local(mid, key) && corium::Prefs::putKept(mid, key, value);
}

// wipe then apply, -1 = bad blob or every row refused
static int apply_import(uint8_t* buf, size_t len, corium::ImportText::Result* out,
                        const char** err_out) {
  const char* err = check_import(buf, len);
  if (err) { if (err_out) *err_out = err; return -1; }

  corium::Prefs::resetAll(/*keep_network=*/true);
  *out = corium::ImportText::applyBlob(buf + BLOB_HEAD, len - BLOB_HEAD - BLOB_CRC, import_kept);
  if (out->skipped) corium::Log::console(PSTR("Import: %d refused, first %s"), out->skipped, out->firstKey);
  // another board's export, say: the old settings stand
  if (out->applied == 0 && out->skipped) {
    corium::Prefs::revert();
    if (err_out) *err_out = PSTR("nothing imported");
    return -1;
  }
  return out->applied;
}

// safe mode gets here frozen: one the import didn't replace stops it all
static const char* import_commit(char* why, size_t n) {
  const char* err = corium::Prefs::anyFrozen(why, n) ? why : nullptr;
  if (!err && !corium::Prefs::commit(false)) err = WRITE_FAILED;   // silent, the cascade would WDT
  if (err) corium::Prefs::revert();
  return err;
}

// a row that didn't load isn't in the blob, an import would lose it
void corium::WebPortal::hExport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  char why[CORIUM_PREFS_FROZEN_MAX];
  if (corium::Prefs::anyFrozen(why, sizeof(why))) {
    res.send(409, FPSTR(HTTP_CT_PLAIN), why);
    return;
  }
  res.beginChunked(200, FPSTR(HTTP_CT_PLAIN));
  B64Sink sink;
  b64_init(sink, res);
  export_stream(sink);
  // crc goes out big endian, after the payload
  uint32_t crc = sink.crc ^ EXPORT_OBF_SEED;
  uint8_t crc_be[BLOB_CRC];
  wrBe32(crc_be, crc);
  for (uint8_t b : crc_be) b64_put(sink, b);
  b64_finish(sink);
  res.endChunked();
}

static BodyBuf s_import_body;

// too big or no memory, hImport says no body
bool corium::WebPortal::hImportBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* chunk, size_t len, void*) {
  return body_event(s_import_body, req, ev, chunk, len, nullptr);
}

static void import_fail(EGHttpResponse& res, const char* msg) {
  corium::WebPortal::beginPage(res, F("Import"));
  char buf[160];
  snprintf_P(buf, sizeof(buf),
             PSTR("<p>Import failed: %s.</p><p><a href=/param>Back</a></p>"), msg);
  res.sendChunk(buf);
  corium::WebPortal::endPage(res);
  body_free(s_import_body);
}

// decoded in the body buffer
static char* import_field(size_t* len, const char** err) {
  if (!s_import_body.ok || !s_import_body.buf || s_import_body.len == 0) {
    *err = PSTR("no body");
    return nullptr;
  }
  char* field = eghttp_form_take(s_import_body.buf, s_import_body.len, PSTR("blob"), len);
  if (!field || *len == 0) {
    *err = PSTR("missing blob");
    return nullptr;
  }
  return field;
}

// malloc'd blob, or nullptr and err
static uint8_t* import_decode(const char* b64, size_t b64_len, unsigned int* raw_len, const char** err) {
  unsigned int raw_cap = decode_base64_length((const unsigned char*)b64, b64_len);
  if (raw_cap == 0 || raw_cap > BODY_MAX) {
    *err = PSTR("bad base64");
    return nullptr;
  }
  uint8_t* raw = (uint8_t*)malloc(raw_cap);
  if (!raw) {
    *err = PSTR("alloc failed");
    return nullptr;
  }
  *raw_len = decode_base64((const unsigned char*)b64, b64_len, raw);
  return raw;
}

// wipe then apply, -1 = bad
static int import_blob(const char* b64, size_t b64_len, corium::ImportText::Result* out,
                       const char** err) {
  unsigned int raw_len = 0;
  uint8_t* raw = import_decode(b64, b64_len, &raw_len, err);
  body_free(s_import_body);
  if (!raw) return -1;
  int applied = apply_import(raw, raw_len, out, err);
  free(raw);
  return applied;
}

// only the listed prefs change, each one validated; -1 = none
static int import_lines(const char* text, size_t len, corium::ImportText::Result* out, const char** err) {
  corium::ImportText::Result r = corium::ImportText::apply(text, len, import_put);
  body_free(s_import_body);
  *out = r;
  if (r.applied == 0) {
    *err = PSTR("nothing imported");
    return -1;
  }
  return r.applied;
}

void corium::WebPortal::hImport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  const char* err = nullptr;
  size_t len = 0;
  corium::ImportText::Result r = {};
  int applied = -1;
  char why[CORIUM_PREFS_FROZEN_MAX];
  // safe mode: the import may replace it
  if (!corium::BootGuard::safeMode() && corium::Prefs::anyFrozen(why, sizeof(why))) {
    import_fail(res, why);
    return;
  }
  char* field = import_field(&len, &err);
  const char* blob = field ? corium::ImportText::blobStart(field, len) : nullptr;
  if (blob) applied = import_blob(blob, len - (size_t)(blob - field), &r, &err);
  else if (field) applied = import_lines(field, len, &r, &err);
  if (applied >= 0) err = import_commit(why, sizeof(why));
  if (applied < 0 || err) {
    import_fail(res, err ? err : PSTR("bad blob"));
    return;
  }
  corium::WebPortal::beginPage(res, F("Imported"));
  char msg[112];
  if (r.firstKey[0]) {
    // blob, a key from the upload
    snprintf_P(msg, sizeof(msg), PSTR("<p>Imported %d prefs, %d refused (first "), applied, r.skipped);
    res.sendChunk(msg);
    char esc[6 * (sizeof(r.firstKey) - 1) + 1];
    egesc_html(esc, sizeof(esc), r.firstKey);
    res.sendChunk(esc);
    snprintf_P(msg, sizeof(msg), PSTR("). Restarting.</p>"));
  } else if (r.skipped) {
    snprintf_P(msg, sizeof(msg), PSTR("<p>Imported %d prefs, skipped %d (first at line %d). Restarting.</p>"),
               applied, r.skipped, r.firstSkip);
  } else {
    snprintf_P(msg, sizeof(msg), PSTR("<p>Imported %d prefs. Restarting.</p>"), applied);
  }
  res.sendChunk(msg);
  corium::WebPortal::sendRestartCountdown(res);
  corium::WebPortal::endPage(res);
  corium::WebPortal::requestRestart();
}

// a chunk per module, a big one can't push the rest out
void corium::WebPortal::hOutputs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  unsigned long now = corium::fastMillis();
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(F("{"));
  char buf[256];
  bool first = true;
  for (uint8_t i = 0; i < corium::Registry::count(); i++) {
    buf[0] = ',';
    size_t n = corium::Outputs::module(i, buf + 1, sizeof(buf) - 1, now);
    if (!n) continue;
    if (first) res.sendChunk(buf + 1, n);
    else       res.sendChunk(buf, n + 1);
    first = false;
  }
  res.sendChunk(F("}"));
  res.endChunked();
}

void corium::WebPortal::hVars(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(F("{"));
  bool first = true;
  for (corium::MeasureIter it(corium::M_OUT_VARS); it.next(); ) {
    const corium::Measure& m = it.row();
    char raw[64];
    if (corium::m_format_display(m, it.value(), raw, sizeof(raw)) == 0) continue;
    bool str = m.kind == corium::M_STR;
    const char* shown = raw;
    char esc[6 * (sizeof(raw) - 1) + 1];   // escaped worst case
    if (str) {
      egesc_json(esc, sizeof(esc), raw);
      shown = esc;
    }
    char head[40];
    int n = snprintf_P(head, sizeof(head),
                       str ? PSTR("%s\"%s\":\"") : PSTR("%s\"%s\":"),
                       first ? PSTR("") : PSTR(","), m.id);
    if (n <= 0 || (size_t)n >= sizeof(head)) continue;
    res.sendChunk(head, (size_t)n);
    res.sendChunk(shown, strlen(shown));
    if (str) res.sendChunk(F("\""));
    first = false;
  }
  res.sendChunk(F("}"));
  res.endChunked();
}

void corium::WebPortal::metricsLine(EGHttpResponse& res, PGM_P fmt, ...) {
  char buf[320];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf_P(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  send_fmt(res, buf, n, sizeof(buf));
}

static void metrics_render_info(EGHttpResponse& res, const char* cid, const char* name) {
  res.sendChunk(F("# HELP device_info Device metadata; value always 1. Join via on(chipid) for naming.\n"
                  "# TYPE device_info gauge\n"));
  char esc_name[3 * CORIUM_NAME_MAX + 1];
  egesc_prom(esc_name, sizeof(esc_name), name);
  corium::WebPortal::metricsLine(res,
    PSTR("device_info{chipid=\"%s\",name=\"%s\",model=\"" APP_MODEL "\",ver=\"" RELEASE_VERSION "\",env=\"" BUILD_ENV "\"} 1\n"),
    cid, esc_name);
}

// temperature in Celsius
static void metrics_render_rows(EGHttpResponse& res, const char* cid) {
  char buf[320];
  const char* last = nullptr;
  for (corium::MeasureIter it(corium::M_OUT_PROM); it.next(); ) {
    const corium::Measure& m = it.row();
    size_t n = corium::m_prom_format(m, it.value(), cid, m.prom != last, buf, sizeof(buf));
    if (n == 0) continue;
    res.sendChunk(buf, n);
    last = m.prom;
  }
}

void corium::WebPortal::hMetrics(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // chipid only, a rename keeps the series
  const char* cid  = corium::DeviceInfo::chipId();
  const char* name = corium::DeviceInfo::displayName();
  res.beginChunked(200, F("text/plain; version=0.0.4"));
  metrics_render_info(res, cid, name);
  metrics_render_rows(res, cid);
  corium::AppHooks::metrics(res, cid);
  res.endChunked();
}

// log lines idx..end-1, one per row
static void send_log(EGHttpResponse& res, uint32_t idx, uint8_t end) {
  char line[256];
  size_t len;
  while ((len = corium::Log::copySince(&idx, end, line, sizeof(line))) > 0) res.sendChunk(line, len);
}

void corium::WebPortal::hWebCmd(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* body = req.body();
  if (!body || !*body) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("empty command"));
    return;
  }
  // reply = the lines it logged
  uint32_t idx = (uint8_t)corium::Log::getLogIdx();
  bool ok = corium::Console::run(body, req.bodyLen());
  res.beginChunked(ok ? 200 : 400, FPSTR(HTTP_CT_PLAIN));
  send_log(res, idx, (uint8_t)corium::Log::getLogIdx());
  res.endChunked();
}

void corium::WebPortal::hConsoleStream(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // "<idx>,<tz_min>\n" then new lines
  int32_t c1 = 0;
  parseI32(req.arg(F("c1")), &c1);
  uint32_t idx = (uint32_t)c1;

  res.addHeader(FPSTR(HTTP_H_CACHE), F("no-store"));
  res.beginChunked(200, FPSTR(HTTP_CT_PLAIN));

  // read once, sendChunk can yield
  uint8_t end = (uint8_t)corium::Log::getLogIdx();
  char hdr[24];
  send_fmt(res, hdr, snprintf_P(hdr, sizeof(hdr), PSTR("%u,%d\n"),
                                (unsigned)end, corium::Clock::tzOffsetMin), sizeof(hdr));
  send_log(res, idx, end);
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
