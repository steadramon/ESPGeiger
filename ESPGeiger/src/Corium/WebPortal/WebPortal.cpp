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
#include "NtpPage.h"
#include "Outputs.h"
#include "PortalPaths.h"
#include "Corium/Console/Console.h"
#include "Corium/Module/Module.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Prefs/SystemPrefs.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/BootGuard.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Net/WifiInfo.h"
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

#include "STYLE_CSS.gz.h"
#include "THEME_JS.gz.h"

#define CACHE_1D "public, max-age=86400"
#define CACHE_7D "public, max-age=604800"
extern const char wifiAdvancedHTML[] PROGMEM;

// CSS + page templates

#if !EG_GZ_STYLE_CSS
static const char STYLE_CSS[] PROGMEM = R"CSS(:root{--bg:#fff;--page:#f8f9fa;--fg:#212529;--muted:#687179;--border:#dee2e6;--accent:#06c;--link:#06c;--card:#f8f9fa}
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
input:focus,select:focus,textarea:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 2px #06c}
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
.il{display:inline-block;margin:.4em 1em .4em 0}.il input{width:auto;margin-right:.3em;vertical-align:middle}
small{display:block;color:var(--muted);font-size:.85em;margin:.1em 0 .4em}
hr{border:0;border-top:1px solid var(--border);margin:1em 0}
h4{margin:.8em 0 .2em;font-size:1em;color:var(--muted)}
output{display:inline-block;margin-left:.6em;color:var(--muted)}
details.bx{border:1px solid var(--border);border-radius:4px;padding:.6em .8em;margin:.6em 0}
details.bx>summary{font-weight:500;cursor:pointer;color:var(--muted)}
details.adv{margin:.5em 0 0;border:0;padding:0}details.adv>summary{color:var(--muted);font-size:.85em;font-weight:400}
.gb{font-size:.68em;font-weight:400;padding:.05em .45em;border-radius:3px;margin-left:.5em;border:1px solid var(--border);color:var(--muted)}.gb.on{color:var(--link);border-color:var(--link)}
nav.tabs{display:flex;border-bottom:1px solid var(--border);margin:0 0 1em;gap:.4em;flex-wrap:wrap}
nav.tabs a{padding:.5em .9em;color:var(--muted);border-bottom:2px solid transparent;text-decoration:none}
nav.tabs a:hover{color:var(--fg)}
nav.tabs a.ac{color:var(--link);border-bottom-color:var(--link)}
.mt{margin-top:.4em}
#dyB{position:fixed;top:0;left:0;width:100%;padding:.5em;background:#fc3;color:#000;text-align:center;z-index:9999;cursor:pointer;font-size:.9em;font-weight:600}
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

static void send_head_tail(EGHttpResponse& res) {
  res.sendChunk(FPSTR(corium::BootGuard::safeMode() ? SAFE_THEME : HEAD_THEME));
  res.sendChunk(FPSTR(HEAD_H1));
  res.sendChunk(FPSTR(FAVICON_PATHS));
  res.sendChunk(FPSTR(SVG_END));
}

static void send_safe_tag(EGHttpResponse& res) {
  if (corium::BootGuard::safeMode()) res.sendChunk(F(" <b>SAFE MODE</b>"));
}

static void send_safe_banner(EGHttpResponse& res) {
  if (!corium::BootGuard::safeMode()) return;
  uint32_t up = corium::fastMillis();
  uint32_t at = corium::BootGuard::retryAfterMs();
  // minutes left, rounded up
  corium::WebPortal::sendf(res,
      PSTR("<p>Normal mode again in %u min, or <a href=/restart>restart now</a>.</p>"),
      (unsigned)(up < at ? (at - up) / 60000 + 1 : 0));
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
  {"/restart", "Restart"},
  {"", ""}
};

static void render_menu(EGHttpResponse& res, const corium::MenuEntry* e) {
  for (; e; e++) {
    corium::MenuEntry m;
    memcpy_P(&m, e, sizeof(m));
    if (!m.path[0]) break;
    corium::WebPortal::sendf(res, PSTR("<a href='%s'>%s</a>"), m.path, m.label);
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
  if (corium::Wifi::testState() == EGPortal::JOIN_OK) (void)corium::Wifi::keepJoined(WIFI_MOVED_MS);   // logs
}

void corium::WebPortal::poll(uint32_t now) {
  // unsigned diff, wrap safe
  if (now - _poll_last_ms < pollGap()) return;
  _poll_last_ms = now;

  if (_active) _http.tick();
  if (s_ap_test && !corium::Wifi::testing()) ap_test_end();

  if (corium::Lifecycle::restartDue(now)) {
    bool wrote = s_wifi_save || s_erase_creds || s_factory_reset;
    if (s_wifi_save) {
      corium::Log::console(PSTR("WebPortal: Applying new WiFi creds (ssid='%s')"),
                   corium::Wifi::heldSsid());
      (void)corium::Wifi::beginHeld();   // held since the save
      s_wifi_save = false;
    }
    if (s_erase_creds) {
      corium::Wifi::clearSavedCreds();
      s_erase_creds = false;
    }
    if (s_factory_reset) {
      corium::DeviceInfo::factoryReset();
      s_factory_reset = false;
    }
    if (wrote) delay(150);  // restart follows, NVS settle
    corium::Log::console(PSTR("WebPortal: Restarting"));
    corium::DeviceInfo::safeRestart(50);
  }
}


const char P_PARAM[]   PROGMEM = "/param";
static const char P_UPDATE[]  PROGMEM = "/update";
static const char P_IMPORT[]  PROGMEM = "/import";
const char P_RESTART[] PROGMEM = "/restart";

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
    corium::Log::console(PSTR("Clock: Set from browser"));
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
    if (!corium::Registry::quarantined(m)) m->registerRoutes(_http);
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
  char kept[WEB_PASS_MAX + 1];
  const char* cur = corium::SystemPrefs::webPass(kept);
  static_assert(sizeof("admin") - 1 + 1 + WEB_PASS_MAX <= EGHTTP_AUTH_MAX,
                "admin:pass must fit EGHTTP_AUTH_MAX or auth switches off");
  p->_http.setBasicAuth(F("admin"), cur, F(THING_NAME));
  p->_http.setAuthHelp(corium::AUTH_HELP);
  p->_http.setSameOrigin(corium::Prefs::getBool("sys", corium::K_ORIGIN));
  static int8_t s_auth = -1;
  int8_t auth = *cur ? 1 : 0;
  if (auth != s_auth) {
    corium::Log::console(PSTR("WebPortal: Auth %s"),
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
  res.sendKV(F(" | "), esc);
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
    if (!corium::Registry::quarantined(m)) render_menu(res, m->menuEntries());
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
go=()=>{var c=new AbortController(),x=setTimeout(()=>c.abort(),ms+5e3);
fetch(typeof u=='function'?u():u,{cache:'no-store',signal:c.signal}).then(r=>r.ok?r.text():null).catch(()=>null).then(t=>{clearTimeout(x);L=Date.now();var w;try{w=fn(t)}catch(e){}k(w>=0?w:t==null?6e3:ms)})};
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

void corium::WebPortal::endRestartPage(EGHttpResponse& res, uint32_t delay_ms) {
  sendf(res, PSTR("<script>window._rs=%u;</script>"), (unsigned)RESTART_COUNTDOWN_SECS);
  res.sendChunk(FPSTR(RESTART_COUNTDOWN));
  endPage(res);
  requestRestart(delay_ms);
}

// a group holding a value that didn't load says which
void corium::WebPortal::sendNotSaved(EGHttpResponse& res) {
  res.sendChunk(F("<div class='a ae'>Not saved: "));
  char why[CORIUM_PREFS_FROZEN_MAX];
  if (corium::Prefs::frozen(why, sizeof(why))) res.sendChunk(why);
  else res.sendChunk(FPSTR(corium::WRITE_FAILED));
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
  corium::WebPortal::endRestartPage(res);
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

void corium::WebPortal::hReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  s_factory_reset = true;
  corium::WebPortal::beginPage(res, F("Factory reset"));
  res.sendChunk(F("<p>All settings cleared. Device is restarting and will return to the setup portal.</p>"));
  corium::WebPortal::endRestartPage(res);
}

void corium::WebPortal::hEraseWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // back to DHCP for the setup AP, a net row that didn't load goes too
  corium::Log::console(PSTR("WebPortal: Erasing WiFi credentials"));
  corium::Prefs::reset("net");
  if (!corium::Prefs::commit()) {
    not_saved_page(res, F("Erase WiFi"));
    return;
  }
  corium::WebPortal::beginPage(res, F("Erase WiFi"));
  res.sendChunk(F("<p>WiFi credentials erased and network reset to DHCP. Restarting into the setup portal.</p>"));
  corium::WebPortal::endPage(res);
  s_erase_creds = true;
  corium::WebPortal::requestRestart();
}

// /wifi + /wifisave

static const char WIFI_FORM_HEAD[] PROGMEM = R"HTML(
<form method=POST action=/wifisave>
<div style='display:flex;align-items:center;justify-content:space-between'>
<label for=ws>Network</label>
<button type=button id=rescan class='btn-sm btn-o'>Rescan</button>
</div>
<select id=ws onchange="byID('s').value=this.options[this.selectedIndex].dataset.ssid||''">
<option value=''>Scanning&hellip;</option>
</select>
<label for=s>SSID</label>
<input id=s name=ssid maxlength=)HTML" STR(EGPORTAL_SSID_MAX) R"HTML( placeholder='(blank to keep current network)'>
<label for=p>Password</label>
<input id=p name=password type=password maxlength=)HTML" STR(EGPORTAL_PASS_MAX) R"HTML( placeholder='(blank for open networks)'>
)HTML";

static const char WIFI_FORM_END[] PROGMEM = R"HTML(
<button type=submit>Save and restart</button>
</form>
<p class=muted style='margin-top:1em'>If the new network is unreachable, the previous one is restored automatically after about a minute.</p>
)HTML";

static void wifi_ip_field(EGHttpResponse& res, const char* key, PGM_P label, PGM_P hint) {
  static const char IPV4_PATTERN_ATTR[] PROGMEM =
    " pattern='^(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)(\\.(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)){3}$' value='";
  char esc[6 * CORIUM_IPV4_MAX + 1];
  egesc_html(esc, sizeof(esc), corium::Prefs::getString("net", key));
  corium::WebPortal::sendf(res, PSTR("<label for=%s>%s</label><input id=%s name=%s maxlength="
                                     STR(CORIUM_IPV4_MAX) " placeholder='%s'"),
                           key, label, key, key, hint);
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

  wifi_ip_field(res, corium::K_IP, PSTR("IP address"), PSTR("192.168.1.50"));
  wifi_ip_field(res, corium::K_GW, PSTR("Gateway"), PSTR("192.168.1.1"));
  wifi_ip_field(res, corium::K_SN, PSTR("Subnet mask"), PSTR("255.255.255.0"));
  wifi_ip_field(res, corium::K_DNS,
                PSTR("DNS server <span class=muted style='font-weight:normal'>(optional)</span>"),
                PSTR("same as gateway"));
  res.sendChunk(FPSTR(W_DETAILS_END));
}

// no clock yet, the browser has one. id=time, /status links here
static const char CLOCK_SET[] PROGMEM =
  "<p id=time>Clock not set <button type=button class='btn-sm btn-o' onclick=\"fetch('/clock',{method:'POST',"
  "body:new URLSearchParams({e:Math.floor(Date.now()/1e3)})}).then(()=>location.reload())\">"
  "Set from this browser</button></p>";

void corium::WebPortal::hWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Network"));

  char ssid[6 * EGPORTAL_SSID_MAX + 1];
  egesc_html(ssid, sizeof(ssid), corium::Wifi::isConnected() ? corium::Wifi::SSID().c_str() : "");
  sendf(res, W_CONNECTED, ssid[0] ? ssid : PSTR("(none)"));
  res.sendChunk(F("</b></p>"));

  res.sendChunk(FPSTR(WIFI_FORM_HEAD));
  wifi_render_static_ip(res);
  res.sendChunk(FPSTR(WIFI_FORM_END));

  if (!corium::Clock::synced) res.sendChunk(FPSTR(CLOCK_SET));
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
    int open = corium::Wifi::encOpen(aps[i].enc) ? 1 : 0;
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
    res.sendKV(F("<p>Saving credentials for <b>"), ssid, F("</b> and restarting.</p>"));
    res.sendChunk(F("<p class=muted>If the new network is reachable, the device will "
                    "reconnect on its own. If it isn't, the previous credentials are "
                    "restored automatically after about a minute.</p>"));
  } else {
    res.sendChunk(F("<p>Saving network settings and restarting.</p>"
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

// the 400 a form fix answers
static void bad_settings(EGHttpResponse& res, PGM_P what, const char* why) {
  char body[160];
  snprintf_P(body, sizeof(body), PSTR("Bad %s: %s\nClick Back to fix it.\n"), what, why);
  res.send(400, FPSTR(HTTP_CT_PLAIN), body);
}

// BUSY is the sdk's
enum HoldResult : uint8_t { HELD, TOO_LONG, BAD_PASS, NO_MEMORY, IN_TEST };

// only with an ssid, none keeps the old one
static HoldResult wifi_hold_args(EGHttpRequest& req) {
  char s[EGPORTAL_SSID_MAX + 1], p[EGPORTAL_PASS_MAX + 1];
  EGPortal::Creds c = EGPortal::takeCreds(req, s, p);
  if (c == EGPortal::CREDS_PASS) return BAD_PASS;
  if (c != EGPortal::CREDS_OK) return TOO_LONG;
  switch (corium::Wifi::holdCreds(s, p)) {
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
    bad_settings(res, PSTR("WiFi settings"), PSTR("SSID or password too long"));
    return false;
  }
  if (held == BAD_PASS) {
    bad_settings(res, PSTR("WiFi settings"), PSTR("password must be 8-63 characters, or 64 hex digits"));
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
  bad_settings(res, PSTR("network config"), err);
  return true;
}

// backup first, boot can revert a static. false = not all put
CORIUM_NODISCARD static bool put_net(bool st, bool join, const char* ip, const char* gw,
                                     const char* sn, const char* dns) {
  if (!corium::Wifi::saveNetBackup() && st) return false;
  const char* f[] = {st ? "1" : "0", ip, gw, sn, dns};
  // hotspot mode: only once joined
  return corium::Wifi::putNet(f) && (!join || corium::Wifi::ap || corium::Wifi::joinMode());
}

static void ap_test_save(EGHttpResponse& res);

// saved, restart, the backup reverts it; hotspot mode tests in place
static void wifi_saved(EGHttpResponse& res, bool has_ssid) {
  if (has_ssid) {
    if (corium::Wifi::ap) {
      ap_test_save(res);
      return;
    }
    (void)corium::Wifi::saveBackupCreds();   // logged; the portal is the way back
    corium::BootGuard::requestPortal();   // offline builds too, once
    s_wifi_save = true;
  }
  corium::WebPortal::beginPage(res, F("Saving WiFi"));
  wifi_render_save_msg(res, has_ssid);
  corium::WebPortal::endRestartPage(res, WIFI_RESTART_MS);
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
    bad_settings(res, PSTR("network config"), PSTR("not an IP address"));
    return;
  }

  if (static_on && static_refused(res, ip, gw, sn)) return;

  const char* ssid = req.arg(F("ssid"));
  bool has_ssid = (ssid && *ssid);
  HoldResult held = has_ssid ? wifi_hold_args(req) : HELD;
  if (!wifi_held_ok(res, held)) return;

  bool put = put_net(static_on, has_ssid, ip, gw, sn, dns);
  if (!corium::Prefs::commit() || !put) {
    if (has_ssid) corium::Wifi::dropHeld();
    not_saved_page(res, F("Saving WiFi"));
    return;
  }
  wifi_saved(res, has_ssid);
}

// a chunk per module, a big one can't push the rest out
void corium::WebPortal::hOutputs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  res.sendChunk(F("{"));
  char buf[256];
  bool first = true;
  for (uint8_t i = 0; i < corium::Registry::count(); i++) {
    buf[0] = ',';
    size_t n = corium::Outputs::module(i, buf + 1, sizeof(buf) - 1);
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

void corium::WebPortal::sendf(EGHttpResponse& res, PGM_P fmt, ...) {
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
  char esc_name[3 * CORIUM_NAME_MAX + 1];
  egesc_prom(esc_name, sizeof(esc_name), name);
  corium::WebPortal::sendf(res,
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
  corium::WebPortal::sendf(res, PSTR("%u,%d\n"), (unsigned)end, corium::Clock::tzOffsetMin);
  send_log(res, idx, end);
  res.endChunked();
}

extern const char wifiAdvancedHTML[] PROGMEM = R"HTML(
<details style='margin-top:1.5em'><summary>Advanced</summary>
<p class=muted>Erase the saved WiFi credentials and restart into the setup portal.</p>
<form method=POST action=/erase onsubmit="return confirm('Erase saved WiFi credentials? Device will restart into the setup portal.')">
<button type=submit class=danger>Erase WiFi</button>
</form>
<p class=muted style=margin-top:1em>Factory reset: erase <b>all</b> settings and calibration, then restart into the setup portal.</p>
<form method=POST action=/reset onsubmit="return confirm('Erase ALL settings and restart into the setup portal? This cannot be undone.')">
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
