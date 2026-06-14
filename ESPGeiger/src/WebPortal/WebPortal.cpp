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
#include "../GRNG/GRNG.h"
#include "../Module/EGModule.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Counter/Counter.h"
#include "../GeigerInput/GeigerInput.h"
#include "../Util/DeviceInfo.h"
#include "../Util/CrashDump.h"
#include "../Util/Wifi.h"
#include "../Util/OutputVars.h"
#include "../Util/StringUtil.h"
#include <EGPortal.h>
#include "../Logger/Logger.h"
#include "../NTP/NTP.h"
#include "../SerialCommand/SerialCommand.h"
#include <LittleFS.h>
#include <EGBase64.h>

extern SerialCommand serialcmd;
#ifdef ESP8266
  #include <ESP8266WiFi.h>
  #include <user_interface.h>
#else
  #include <WiFi.h>
  #include <Update.h>   // ESP8266 picks Update up via ESP.h; ESP32 needs explicit include
#endif

extern Counter gcounter;

#include "STYLE_CSS.gz.h"
#include "THEME_JS.gz.h"
#include "JS_BUNDLE.gz.h"
#include "HVJS_BUNDLE.gz.h"

// PROGMEM JS blobs are defined at the bottom of this file. Forward-declare
// so the handlers above can reference them.
extern const char picographJS[] PROGMEM;

#define CACHE_1D "public, max-age=86400"
#define CACHE_7D "public, max-age=604800"
#define CACHE_IMMUTABLE "public, max-age=31536000, immutable"
extern const char statusJS[]    PROGMEM;

// ---------- shared CSS + page templates (PROGMEM) ----------

#if !EG_GZ_STYLE_CSS
static const char STYLE_CSS[] PROGMEM = R"CSS(:root{--bg:#fff;--page:#f8f9fa;--fg:#212529;--muted:#6c757d;--border:#dee2e6;--accent:#06c;--card:#f8f9fa}
:root[data-theme=dark]{--bg:#212529;--page:#1a1d20;--fg:#dee2e6;--muted:#adb5bd;--border:#373b3e;--card:#2b3035}
*{box-sizing:border-box}
html{background:var(--page);min-height:100%}
body{font:16px/1.5 -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;max-width:780px;margin:1.5em auto;padding:1.5em 1em;color:var(--fg);background:var(--bg);border-radius:8px;box-shadow:0 1px 3px #0000001a}
@media(max-width:640px){body{margin:0 auto;border-radius:0;box-shadow:none}}
h1{font-size:1.5em;margin:0 0 1em;font-weight:500}
h2{font-size:.9em;margin:.6em 0 .3em;font-weight:700;color:var(--fg);text-transform:uppercase;letter-spacing:.05em}
a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}
input,select,textarea,button{font:inherit;padding:.5em .7em;border:1px solid var(--border);border-radius:4px;background:var(--bg);color:var(--fg);width:100%}
input[type=radio],input[type=checkbox]{width:auto;margin:0 .4em 0 0;vertical-align:middle}
input:focus,select:focus,textarea:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 2px #0066cc26}
button,input[type=submit]{background:var(--accent);color:#fff;border:0;cursor:pointer;width:auto;padding:.5em 1em}
form>button[type=submit]{margin-top:.8em}
button.danger{background:#c0392b}button.danger:hover{background:#a02a1f}
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
.card-ok{border-color:#5a5}
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
nav.tabs{display:flex;border-bottom:1px solid var(--border);margin:0 0 1em;gap:.4em;flex-wrap:wrap}
nav.tabs a{padding:.5em .9em;color:var(--muted);border-bottom:2px solid transparent;text-decoration:none}
nav.tabs a:hover{color:var(--fg)}
nav.tabs a.ac{color:var(--accent);border-bottom-color:var(--accent)}
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
:root.crt{--bg:#000;--page:#000;--fg:#0f0;--muted:#080;--border:#050;--accent:#0a0;--card:#001a00}
:root.crt[data-theme=light]{--bg:#212529;--card:#2b3035;--border:#373b3e;background:#1a1d20}
:root.crt[data-theme=dark]{background:#000}
:root.crt body{font-family:Consolas,Menlo,monospace;font-weight:600;box-shadow:none}
:root.crt :is(h1,h2,h3){color:#0f0;text-shadow:0 0 4px #0f0;font-weight:700}
:root.crt :is(th,td,p,span,div){font-weight:600}
:root.crt :is(button,input[type=submit],.menu a,.back){color:#000;text-shadow:none}
:root.crt .devhead h1 .tag{color:#0a0;text-shadow:none;opacity:1}
:root.crt::after{content:'';position:fixed;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.18) 0,rgba(0,0,0,.18) 1px,transparent 1px,transparent 3px);z-index:9998;animation:crtroll 4s linear infinite}
@keyframes crtroll{to{background-position:0 3px}})CSS";
#endif

// Trefoil path data, single flash copy. Wrapped at emission time with either
// the standalone <svg xmlns=...> (for /favicon.svg, tab icon) or the inline
// <svg style=...> (for the page-header H1 logo).
static const char FAVICON_PATHS[] PROGMEM =
  "<circle cx='256' cy='256' r='256' fill='#666'/>"
  "<circle cx='256' cy='256' r='220' fill='#FB2'/>"
  "<path fill='#555' d='M256 286a30 30 0 1 0 0-60 30 30 0 0 0 0 60zm28-82 62-109a182 182 0 0 0-182 1l63 109a57 57 0 0 1 57-1zm155 51H313c0 21-11 39-28 49l64 108c54-32 90-90 90-157zM163 412l64-108a57 57 0 0 1-28-49H73c0 67 36 125 90 157z'/>";

static const char PAGE_TAIL[] PROGMEM =
  "<p class=back-row><a class=back href=/>\xe2\x86\x90 Home</a></p>"
  "</body></html>";

// Shared with hRoot to dedupe.
static const char HEAD_OPEN[] PROGMEM =
  "<!DOCTYPE html><html lang=en><head>"
  "<meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<link rel=stylesheet href=/style.css" EG_CACHE_BUST ">"
  "<link rel=icon type=image/svg+xml href=/favicon.svg>"
  "<title>";

// Page chrome up to the inline-SVG opening tag. FAVICON_PATHS + "</svg>"
// follow via sendHeadTail() to keep the path data flash-singleton.
static const char HEAD_TAIL[] PROGMEM =
  "</title><script src=/theme.js" EG_CACHE_BUST "></script></head><body>"
  "<button class=themetoggle aria-label=\"Toggle theme\" onclick=theme()></button>"
  "<div class=devhead>"
  "<h1><svg viewBox='0 0 512 512' style='width:1.4em;height:1.4em;flex:0 0 auto'>";

static void sendHeadTail(EGHttpResponse& res) {
  res.sendChunk(FPSTR(HEAD_TAIL));
  res.sendChunk(FPSTR(FAVICON_PATHS));
  res.sendChunk(F("</svg>"));
}

// Cross-cutting menu entries owned here, not by any module.
static const EGMenuEntry WEBPORTAL_TOP_MENU[] = {
  {"/status",  "Status"},
  {"/hist",    "History"},
  {"/param",   "Config"},
  {"/network", "Network Config"},
  {nullptr, nullptr}
};
static const EGMenuEntry WEBPORTAL_BOTTOM_MENU[] = {
  {"/info",    "Info"},
  {"/update",  "Update"},
  {"/restart", "Reboot"},
  {nullptr, nullptr}
};

static void renderMenuButton(EGHttpResponse& res, const EGMenuEntry& e) {
  char btn[160];
  int n = snprintf_P(btn, sizeof(btn),
    PSTR("<a href='%s'>%s</a>"), e.path, e.label);
  size_t len = (n > 0) ? (size_t)n : 0;
  if (len >= sizeof(btn)) len = sizeof(btn) - 1;
  if (len) res.sendChunk(btn, len);
}

// ---------- pending-restart machinery ----------
// Handlers request via requestRestart(); tick() fires ESP.restart() after
// the delay so in-flight responses drain (vs killing them from callback ctx).

static volatile uint32_t s_restartAt = 0;

void WebPortal::requestRestart(uint32_t delayMs) {
  uint32_t at = fast_millis() + delayMs;
  if (at == 0) at = 1;          // 0 means "no restart pending"
  s_restartAt = at;
}

// Pending WiFi save: handler stashes new SSID/password; tick() applies +
// restarts. Doing this *here* (not in the handler) lets the response page
// drain to the browser before WiFi.begin() drops the current STA assoc.
static volatile bool s_pendingWifiSave = false;
static char s_pendingWifiSsid[33] = {0};
static char s_pendingWifiPass[65] = {0};

// Static pointer to the active server. Lets the static
// applyAuthFromPrefs() reach the right instance from SystemPrefs hooks
// without WebPortal having to be an EGModule itself.
static WebPortal* s_activeWebPortal = nullptr;

void WebPortal::tick(uint32_t now) {
  static uint32_t last_ms = 0;
  if (now - last_ms < 13) return;
  last_ms = now;

  if (_active) _http.tick();

  if (s_restartAt != 0 && (int32_t)(now - s_restartAt) >= 0) {
    if (s_pendingWifiSave) {
      Log::console(PSTR("WebPortal: applying new WiFi creds (ssid='%s')"),
                   s_pendingWifiSsid);
      WiFi.persistent(true);
      WiFi.begin(s_pendingWifiSsid, s_pendingWifiPass);
      s_pendingWifiSave = false;
      delay(150);                  // let SDK NVS write settle
    }
    Log::console(PSTR("WebPortal: restarting"));
    DeviceInfo::safeRestart(50);
  }
}

// EGModule subclasses self-register via registerRoutes(); GRNG and Counter
// aren't EGModules so we call them explicitly.

namespace GRNGRoutes    { void registerRoutes(EGHttpServer& http); }
namespace CounterRoutes { void registerRoutes(EGHttpServer& http); }

// ---------- WebPortal ----------

void WebPortal::begin(uint16_t port) {
  if (_active) return;
  _http.on("/",            EGHttpRequest::GET, &WebPortal::hRoot,        this);
  _http.on("/style.css",   EGHttpRequest::GET, &WebPortal::hStyleCss,    nullptr);
  _http.on("/favicon.svg", EGHttpRequest::GET, &WebPortal::hFavicon,     nullptr);
  _http.on("/favicon.ico", EGHttpRequest::GET, &WebPortal::hFavicon,     nullptr);
  _http.on("/theme.js",    EGHttpRequest::GET, &WebPortal::hThemeJs,     nullptr);
  _http.on("/restart",   EGHttpRequest::GET, &WebPortal::hRestart,       nullptr);
  _http.on("/reset",     EGHttpRequest::GET, &WebPortal::hReset,         nullptr);
  _http.on("/erase",     EGHttpRequest::GET, &WebPortal::hEraseWifi,     nullptr);
  _http.on("/info",      EGHttpRequest::GET, &WebPortal::hInfo,          nullptr);
  _http.on("/about",     EGHttpRequest::GET, &WebPortal::hAbout,         nullptr);
  _http.on("/status",    EGHttpRequest::GET, &WebPortal::hStatus,        nullptr);
  _http.on("/js",        EGHttpRequest::GET, &WebPortal::hJs,            nullptr);
  _http.on("/cs",        EGHttpRequest::GET, &WebPortal::hConsoleStream, nullptr);
  _http.on("/webcmd",    EGHttpRequest::POST, &WebPortal::hWebCmd,       nullptr);
  _http.on("/outputs",   EGHttpRequest::GET, &WebPortal::hOutputs,       nullptr);
  _http.on("/metrics",   EGHttpRequest::GET, &WebPortal::hMetrics,       nullptr);
  _http.on("/vars.json", EGHttpRequest::GET, &WebPortal::hVars,          nullptr);
  _http.on("/param",     EGHttpRequest::GET, &WebPortal::hParam,         nullptr);
  _http.on("/param",     EGHttpRequest::POST, &WebPortal::hParam,        nullptr);
  _http.onUpload("/param", &WebPortal::hParamBody, nullptr);
  _http.on("/update",    EGHttpRequest::GET,  &WebPortal::hUpdateForm,   nullptr);
  _http.on("/update",    EGHttpRequest::POST, &WebPortal::hUpdateDone,   nullptr);
  _http.onUpload("/update", &WebPortal::hUpdateBody, nullptr);
  _http.on("/network",   EGHttpRequest::GET,  &WebPortal::hWifi,         nullptr);
  _http.on("/wifiscan",  EGHttpRequest::GET,  &WebPortal::hWifiScan,     nullptr);
  _http.on("/wifisave",  EGHttpRequest::POST, &WebPortal::hWifiSave,     nullptr);
  _http.on("/export",    EGHttpRequest::GET,  &WebPortal::hExport,       nullptr);
  _http.on("/import",    EGHttpRequest::POST, &WebPortal::hImport,       nullptr);
  _http.onUpload("/import", &WebPortal::hImportBody, nullptr);

  // Non-EGModule routes (explicit).
  GRNGRoutes::registerRoutes(_http);
  CounterRoutes::registerRoutes(_http);

  // EGModule routes (auto-discovered).
  uint8_t n = EGModuleRegistry::count();
  for (uint8_t i = 0; i < n; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (m) m->registerRoutes(_http);
  }

  _http.begin(port);
  _active = true;
  s_activeWebPortal = this;
  applyAuthFromPrefs();          // pick up sys.web_pass set during boot
}

void WebPortal::applyAuthFromPrefs() {
  WebPortal* p = s_activeWebPortal;
  if (!p || !p->_active) return;
  const char* cur = EGPrefs::getString("sys", "web_pass");
  p->_http.setBasicAuth("admin", cur);
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
  // <title>: "<page>  <friendly-or-hostname>". h1: <favicon> ESPGeiger - <page>.
  const char* fname = DeviceInfo::friendlyName();
  const char* devName = (fname && fname[0]) ? fname : DeviceInfo::hostname();
  res.sendChunk(FPSTR(HEAD_OPEN));
  res.sendChunk(title);
  res.sendKV(F(" \xc2\xb7 "), devName);
  sendHeadTail(res);
  res.sendChunk(F(THING_NAME " - "));
  res.sendChunk(title);
  if (inlineSub && inlineSub[0]) {
    char buf[80];
    int n = snprintf_P(buf, sizeof(buf),
      PSTR("<span class=tag>%s</span>"), inlineSub);
    if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);
  }
  res.sendChunk(F("</h1></div>"));
}

void WebPortal::sendPageTail(EGHttpResponse& res) {
  res.sendChunk(FPSTR(PAGE_TAIL));
}

void WebPortal::sendPageFooter(EGHttpResponse& res) {
  res.sendKV(F("<p class=cred>"
               "<a class=brand href='https://github.com/steadramon/ESPGeiger' target=_blank rel=noopener>ESPGeiger</a>"
               " &middot; <a href='/about'>"),
             RELEASE_VERSION,
             F("</a></p>"));
}

void WebPortal::hRoot(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");

  // Home page identity. Friendly name (if set) takes precedence over the
  // baked-in build variant name. Never uses input.geiger_model.
#ifdef ESPGEIGER_HW
  #define WEBPORTAL_BASE_NAME "ESPGeiger-HW"
#elif defined(ESPGEIGER_LT)
  #define WEBPORTAL_BASE_NAME "ESPGeiger-Log"
#else
  #define WEBPORTAL_BASE_NAME THING_NAME
#endif
  const char* fname = DeviceInfo::friendlyName();
  bool hasFriendly = (fname && fname[0]);
  const char* hostname = DeviceInfo::hostname();
  // Direct byte format - avoids the heap String from .toString().
  char ipBuf[16];
  IPAddress ipa = WiFi.localIP();
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ipa[0], ipa[1], ipa[2], ipa[3]);

  // Emitted twice; keep one source of truth.
  auto emitIdentity = [&]() {
    if (hasFriendly) res.sendKV(F(WEBPORTAL_BASE_NAME " - "), fname);
    else             res.sendChunk(F(WEBPORTAL_BASE_NAME));
  };

  res.sendChunk(FPSTR(HEAD_OPEN));
  emitIdentity();
  sendHeadTail(res);
  emitIdentity();
  res.sendKV(F("</h1><div class=sub>"), hostname);
  res.sendKV(F(" &middot; "), ipBuf, F("</div></div>"));

  res.sendChunk(F("<div class=menu>"));
  for (const EGMenuEntry* e = WEBPORTAL_TOP_MENU; e->path; e++) {
    renderMenuButton(res, *e);
  }
  uint8_t mc = EGModuleRegistry::count();
  for (uint8_t i = 0; i < mc; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (!m) continue;
    const EGMenuEntry* e = m->menuEntries();
    while (e && e->path) {
      renderMenuButton(res, *e);
      e++;
    }
  }
  for (const EGMenuEntry* e = WEBPORTAL_BOTTOM_MENU; e->path; e++) {
    renderMenuButton(res, *e);
  }
  res.sendChunk(F("</div>"));
  WebPortal::sendPageFooter(res);
  res.sendChunk(F("</body></html>"));
  res.endChunked();
}

void WebPortal::hStyleCss(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", CACHE_IMMUTABLE);
#if EG_GZ_STYLE_CSS
  res.sendGzipP(200, "text/css", STYLE_CSS_GZ, STYLE_CSS_GZ_LEN);
#else
  res.send(200, "text/css", FPSTR(STYLE_CSS));
#endif
}

void WebPortal::hFavicon(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", CACHE_7D);  // 7 days
  res.beginChunked(200, "image/svg+xml");
  res.sendChunk(F("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>"));
  res.sendChunk(FPSTR(FAVICON_PATHS));
  res.sendChunk(F("</svg>"));
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
if(LS.xd==='1')hh=612193241;
window.xd=hh===612193241;
if(s==='1'||(s==null&&window.xd))d.classList.add('crt');
else if(s==='0')d.classList.remove('crt');
AR();AD();
var msg={612193241:'SGFwcHkgQXByaWwgRm9vbHMhIENsaWNrIHRvIGRpc21pc3MuIFR5cGUgY3J0IGluIHRoZSAvc3RhdHVzIGNtZCBib3ggdG8gZGlzYWJsZS4=',1332614041:'4piiIEhhcHB5IGJpcnRoZGF5IE1hcmllIEN1cmllIC0gYm9ybiAxODY3Lg==',1368767674:'8J+puyBIYXBweSBiaXJ0aGRheSBXaWxoZWxtIFLDtm50Z2VuIC0gYm9ybiAxODQ1Lg=='}[hh];
if(msg&&location.pathname==='/'&&SS.dayDis!=hh&&!byID('dyB')){var p=document.createElement('div');p.id='dyB';p.textContent=atob(msg);p.onclick=()=>{SS.dayDis=hh;p.remove()};var ap=()=>document.body.appendChild(p);document.body?ap():L('DOMContentLoaded',ap)}}
window.theme=()=>{var t=d.dataset.theme=='dark'?'light':'dark';
d.dataset.theme=LS.theme=t;TE()};
d.dataset.theme=LS.theme||(matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
C();L('pageshow',e=>{if(e.persisted)C()});
var k=[38,38,40,40,37,39,37,39,66,65],i=0;
L('keydown',e=>{i=e.keyCode===k[i]?i+1:0;
if(i===k.length){
  LS.crt=d.classList.toggle('crt')?'1':'0';
  AR();AD();TE();i=0;
}})
}();)JS";
#endif

void WebPortal::hThemeJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", CACHE_IMMUTABLE);
#if EG_GZ_THEME_JS
  res.sendGzipP(200, "application/javascript", THEME_JS_GZ, THEME_JS_GZ_LEN);
#else
  res.send(200, "application/javascript", FPSTR(THEME_JS));
#endif
}

static const char RESTART_COUNTDOWN[] PROGMEM = R"HTML(
<p class=muted>Reloading in <span id=ct>5</span>s&hellip;</p>
<script>
var n=5,c=byID('ct');
function p(){
  var a=new AbortController();
  setTimeout(()=>a.abort(),1000);
  fetch('/ping',{cache:'no-store',signal:a.signal})
    .then(r=>location.href='/')
    .catch(()=>setTimeout(p,0));
}
var t=setInterval(function(){
  if(--n>0) c.textContent=n;
  else { clearInterval(t); c.textContent='checking…'; p(); }
},1000);
</script>
)HTML";

void WebPortal::sendRestartCountdown(EGHttpResponse& res) {
  res.sendChunk(FPSTR(RESTART_COUNTDOWN));
}

void WebPortal::hRestart(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Restarting"));
  res.sendChunk(F("<p>Device is restarting. This page will reload once it's back - typically 15-20 seconds for WiFi to come up.</p>"));
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

// ---------- /info ----------

void WebPortal::hInfo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Info"));

  // %S = PROGMEM label so we don't pay SRAM for ~25 labels.
  char rowbuf[256];
  int n;
  #define INFO_ROW(k, fmt, ...) do { \
    n = snprintf_P(rowbuf, sizeof(rowbuf), \
      PSTR("<tr><th>%S</th><td>" fmt "</td></tr>"), \
      (PGM_P)PSTR(k), ##__VA_ARGS__); \
    if (n > 0) res.sendChunk(rowbuf, (size_t)n); \
  } while (0)

  // Direct byte format avoids the heap String from .toString().
  char ip[16], gw[16], sn[16], dns_[16], bssid[20], ssid[33], mac[18];
  char ap_ssid[33], ap_ip[16], ap_mac[18];
  auto fmtIP = [](char* dst, size_t cap, IPAddress a) {
    snprintf(dst, cap, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
  };
  auto fmtMac = [](char* dst, size_t cap, const uint8_t* m) {
    snprintf_P(dst, cap, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
               m[0], m[1], m[2], m[3], m[4], m[5]);
  };
  fmtIP(ip,    sizeof(ip),    WiFi.localIP());
  fmtIP(gw,    sizeof(gw),    WiFi.gatewayIP());
  fmtIP(sn,    sizeof(sn),    WiFi.subnetMask());
  fmtIP(dns_,  sizeof(dns_),  WiFi.dnsIP());
  fmtIP(ap_ip, sizeof(ap_ip), WiFi.softAPIP());
  uint8_t macBuf[6];
  WiFi.macAddress(macBuf);
  fmtMac(mac, sizeof(mac), macBuf);
  WiFi.softAPmacAddress(macBuf);
  fmtMac(ap_mac, sizeof(ap_mac), macBuf);
  const uint8_t* bssidPtr = WiFi.BSSID();
  if (bssidPtr) fmtMac(bssid, sizeof(bssid), bssidPtr);
  else          bssid[0] = '\0';
  // .c_str() on a String temp dangles; copy out before the temp dies.
  strncpy(ssid,    WiFi.SSID().c_str(),       sizeof(ssid)-1);    ssid[sizeof(ssid)-1]='\0';
  strncpy(ap_ssid, WiFi.softAPSSID().c_str(), sizeof(ap_ssid)-1); ap_ssid[sizeof(ap_ssid)-1]='\0';

  const char* ssidShown = ssid[0] ? ssid : "(not connected)";
  res.sendKV(F("<p class=muted>Connected to <b>"), ssidShown);
  res.sendKV(F("</b> with IP <b>"), ip);
  res.sendKV(F("</b> &middot; "), DeviceInfo::chipmodel(), F("</p>"));

  // --- System ---
  res.sendChunk(F("<details open><summary>System</summary><table>"));
  INFO_ROW("Uptime",        "%s",            DeviceInfo::uptimeString());
  INFO_ROW("Chip ID",       "%s",            DeviceInfo::chipid());
#ifdef ESP8266
  INFO_ROW("Platform",      "%s",            "esp8266");
  INFO_ROW("Flash chip ID", "%u",            (unsigned)ESP.getFlashChipId());
  INFO_ROW("Flash size",    "%u bytes",      (unsigned)ESP.getFlashChipSize());
  INFO_ROW("Real flash",    "%u bytes",      (unsigned)ESP.getFlashChipRealSize());
  INFO_ROW("Core version",  "%s",            ESP.getCoreVersion().c_str());
  INFO_ROW("Boot version",  "%u",            (unsigned)ESP.getBootVersion());
#elif defined(ESP32)
  INFO_ROW("Platform",      "%s",            ESP.getChipModel());
  INFO_ROW("Flash size",    "%u bytes",      (unsigned)ESP.getFlashChipSize());
  INFO_ROW("Chip rev",      "%u",            (unsigned)ESP.getChipRevision());
  INFO_ROW("SDK version",   "%s",            ESP.getSdkVersion());
#endif
  INFO_ROW("CPU frequency", "%u MHz",        (unsigned)ESP.getCpuFreqMHz());
  INFO_ROW("Free heap",     "%u bytes",      (unsigned)DeviceInfo::freeHeap());
  INFO_ROW("Sketch size",   "%u / %u bytes", (unsigned)ESP.getSketchSize(),
                                             (unsigned)(ESP.getSketchSize() + ESP.getFreeSketchSpace()));
#ifdef ESP8266
  INFO_ROW("Last reset",    "%s",            ESP.getResetReason().c_str());
  {
    rst_info* ri = ESP.getResetInfoPtr();
    if (ri && ri->reason == REASON_EXCEPTION_RST) {
      INFO_ROW("Exc cause", "%u",     (unsigned)ri->exccause);
      INFO_ROW("Exc PC",    "0x%08x", (unsigned)ri->epc1);
      INFO_ROW("Exc addr",  "0x%08x", (unsigned)ri->excvaddr);
    }
  }
  if (const CrashDump::Snapshot* cd = CrashDump::lastCrash()) {
    INFO_ROW("EPC2",      "0x%08x", (unsigned)cd->epc2);
    INFO_ROW("EPC3",      "0x%08x", (unsigned)cd->epc3);
    INFO_ROW("DEPC",      "0x%08x", (unsigned)cd->depc);
    INFO_ROW("SP",        "0x%08x", (unsigned)cd->sp);
    // Filter the stack snapshot to candidate code pointers (IROM range)
    // so the page shows the few words that addr2line could actually
    // resolve, not a wall of locals.
    char framesbuf[256];
    size_t fp = 0;
    bool any = false;
    for (uint32_t i = 0; i < cd->word_count && fp < sizeof(framesbuf) - 12; i++) {
      uint32_t w = cd->stack[i];
      if (w < 0x40100000u || w >= 0x40300000u) continue;
      int wn = snprintf_P(framesbuf + fp, sizeof(framesbuf) - fp,
                          PSTR("%s0x%08x"), any ? " " : "", (unsigned)w);
      if (wn > 0 && (size_t)wn < sizeof(framesbuf) - fp) fp += (size_t)wn;
      any = true;
    }
    if (any) {
      framesbuf[fp] = '\0';
      INFO_ROW("Stack frames", "<code>%s</code>", framesbuf);
    }
  }
#elif defined(ESP32)
  {
    const char* rstStr;
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:   rstStr = "Power on"; break;
      case ESP_RST_EXT:       rstStr = "External reset"; break;
      case ESP_RST_SW:        rstStr = "Software restart"; break;
      case ESP_RST_PANIC:     rstStr = "Exception/panic"; break;
      case ESP_RST_INT_WDT:   rstStr = "Interrupt watchdog"; break;
      case ESP_RST_TASK_WDT:  rstStr = "Task watchdog"; break;
      case ESP_RST_WDT:       rstStr = "Watchdog"; break;
      case ESP_RST_BROWNOUT:  rstStr = "Brownout"; break;
      case ESP_RST_DEEPSLEEP: rstStr = "Deep sleep wake"; break;
      default:                rstStr = "Unknown"; break;
    }
    INFO_ROW("Last reset", "%s", rstStr);
  }
  if (const CrashDump::Snapshot* cd = CrashDump::lastCrash()) {
    INFO_ROW("Exc cause", "%u",     (unsigned)cd->exccause);
    INFO_ROW("Exc PC",    "0x%08x", (unsigned)cd->epc1);
    INFO_ROW("Exc addr",  "0x%08x", (unsigned)cd->excvaddr);
    if (cd->epc2) INFO_ROW("EPC2", "0x%08x", (unsigned)cd->epc2);
    if (cd->epc3) INFO_ROW("EPC3", "0x%08x", (unsigned)cd->epc3);
    if (cd->sp)   INFO_ROW("SP",   "0x%08x", (unsigned)cd->sp);
    // Backtrace from coredump - all PCs are real code addresses, no need
    // to filter the way we do for the ESP8266 stack snapshot.
    if (cd->word_count) {
      char framesbuf[256];
      size_t fp = 0;
      for (uint32_t i = 0; i < cd->word_count && fp < sizeof(framesbuf) - 12; i++) {
        int wn = snprintf_P(framesbuf + fp, sizeof(framesbuf) - fp,
                            PSTR("%s0x%08x"), i ? " " : "", (unsigned)cd->stack[i]);
        if (wn > 0 && (size_t)wn < sizeof(framesbuf) - fp) fp += (size_t)wn;
      }
      framesbuf[fp] = '\0';
      INFO_ROW("Backtrace", "<code>%s</code>", framesbuf);
    }
  }
#endif
  res.sendChunk(F("</table></details>"));

  // --- WiFi ---
  res.sendChunk(F("<details open><summary>WiFi</summary><table>"));
  INFO_ROW("Connected",      "%s",     WiFi.isConnected() ? "Yes" : "No");
  INFO_ROW("Station SSID",   "%s",     ssid);
  INFO_ROW("Station IP",     "%s",     ip);
  INFO_ROW("Station gateway","%s",     gw);
  INFO_ROW("Station subnet", "%s",     sn);
  INFO_ROW("DNS Server",     "%s",     dns_);
  INFO_ROW("Hostname",       "%s",     DeviceInfo::hostname());
  INFO_ROW("Station MAC",    "%s",     mac);
  INFO_ROW("RSSI",           "%d dBm", (int)Wifi::rssi);
  INFO_ROW("BSSID",          "%s",     bssid);
  INFO_ROW("Autoconnect",    "%s",     WiFi.getAutoReconnect() ? "Enabled" : "Disabled");
  INFO_ROW("AP SSID",        "%s",     ap_ssid[0] ? ap_ssid : "(not active)");
  INFO_ROW("AP IP",          "%s",     ap_ip);
  INFO_ROW("AP MAC",         "%s",     ap_mac);
  res.sendChunk(F("</table></details>"));

  // --- Modules ---
  res.sendChunk(F("<details open><summary>Modules</summary><ul style='columns:2;list-style:none;padding:0'>"));
  {
    uint8_t count = EGModuleRegistry::count();
    for (uint8_t i = 0; i < count; i++) {
      EGModule* m = EGModuleRegistry::get(i);
      if (m == nullptr) continue;
      n = snprintf_P(rowbuf, sizeof(rowbuf), PSTR("<li>%s</li>"), m->name());
      if (n > 0) res.sendChunk(rowbuf, (size_t)n);
    }
    // Display-only entry; not an EGModule and not pushed to WebAPI.
    res.sendChunk(F("<li>portal</li>"));
  }
  res.sendChunk(F("</ul></details>"));

  // --- About ---
  res.sendChunk(F("<details open><summary>About</summary><table>"));
  INFO_ROW("Version",      "%s",    RELEASE_VERSION);
  INFO_ROW("Git",          "%s",    GIT_VERSION);
  INFO_ROW("Build env",    "%s",    BUILD_ENV);
  INFO_ROW("Build date",   "%s %s", __DATE__, __TIME__);
  INFO_ROW("Geiger",       "%s",    DeviceInfo::geigermodel());
  INFO_ROW("Feature flags","0x%04x",(unsigned)DeviceInfo::featureFlags());
  res.sendChunk(F("</table></details>"));

  // Erase WiFi action moved to /wifi page (logical home for network config).

  #undef INFO_ROW
  WebPortal::sendPageTail(res);
  res.endChunked();
}

void WebPortal::hAbout(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "application/json");
  uint8_t macb[6];
  WiFi.macAddress(macb);
  char mac_str[13];
  snprintf_P(mac_str, sizeof(mac_str), PSTR("%02X%02X%02X%02X%02X%02X"),
    macb[0], macb[1], macb[2], macb[3], macb[4], macb[5]);

  char buf[400];
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("{\"ver\":\"%s\",\"git\":\"%s\",\"env\":\"%s\","
         "\"date\":\"%s %s\",\"chip\":\"%s\",\"mac\":\"%s\","
         "\"host\":\"%s\",\"g_mod\":\"%s\","
         "\"g_type\":\"%s\",\"g_test\":%s,\"g_pcnt\":%s,"
         "\"modules\":["),
    RELEASE_VERSION, GIT_VERSION, BUILD_ENV,
    __DATE__, __TIME__,
    DeviceInfo::chipmodel(),
    mac_str,
    DeviceInfo::hostname(),
    DeviceInfo::geigermodel(),
    GEIGER_IS_PULSE(GEIGER_TYPE)  ? "pulse" :
    GEIGER_IS_SERIAL(GEIGER_TYPE) ? "serial" : "none",
    GEIGER_IS_TEST(GEIGER_TYPE)   ? "true" : "false",
    gcounter.has_pcnt()           ? "true" : "false");
  size_t len = (n > 0) ? (size_t)n : 0;
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  if (len) res.sendChunk(buf, len);

  uint8_t count = EGModuleRegistry::count();
  for (uint8_t i = 0; i < count; i++) {
    EGModule* m = EGModuleRegistry::get(i);
    if (m == nullptr) continue;
    char modbuf[40];
    int mn = snprintf_P(modbuf, sizeof(modbuf),
      PSTR("%s\"%s\""), (i > 0 ? "," : ""), m->name());
    size_t mlen = (mn > 0) ? (size_t)mn : 0;
    if (mlen >= sizeof(modbuf)) mlen = sizeof(modbuf) - 1;
    if (mlen) res.sendChunk(modbuf, mlen);
  }
  res.sendChunk(F("]}"));
  res.endChunked();
}

void WebPortal::hReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  DeviceInfo::factoryReset();
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Reset"));
  res.sendChunk(F("<p>All settings cleared. Device is restarting and will return to the setup portal.</p>"));
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

void WebPortal::hEraseWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // WiFi-only erase: clear stored creds and revert network config to DHCP
  // so the setup-mode portal doesn't try to bring up a static IP on the
  // captive AP. Other prefs (NTP, HV, WebAPI) survive.
  Log::console(PSTR("WebPortal: erasing WiFi credentials"));
  Wifi::clearSavedCreds();
  EGPrefs::put("net", "static_ip", "0");
  EGPrefs::commit();
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Erase WiFi"));
  res.sendChunk(F("<p>WiFi credentials erased and network reset to DHCP. Device is restarting into setup mode.</p>"));
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

// ---------- /wifi + /wifisave ----------
// Sync scan blocks ~2s. Save defers WiFi.begin() to tick() so the response
// drains before the STA association drops.

void WebPortal::hWifi(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Network"));

  // Show currently-connected network (if any). One String alloc, not two.
  String curSsid = WiFi.SSID();
  char head[160];
  int n = snprintf_P(head, sizeof(head),
    PSTR("<p class=muted>Connected to <b>%s</b></p>"),
    curSsid.length() ? curSsid.c_str() : "(none)");
  if (n > 0) res.sendChunk(head, (size_t)n);

  // Form renders immediately with no scan; JS below polls /wifiscan in
  // the background and populates the dropdown when results arrive. User
  // can type a manual SSID and submit without waiting.
  res.sendChunk(F(R"HTML(
<form method=POST action=/wifisave>
<label style='display:flex;align-items:center;justify-content:space-between'>
  <span>Network</span>
  <button type=button id=rescan class=btn-sm style='background:transparent;color:var(--accent);border:1px solid var(--accent)'>Rescan</button>
</label>
<select id=ws onchange="byID('s').value=this.options[this.selectedIndex].dataset.ssid||''">
<option value=''>Scanning&hellip;</option>
</select>
<label for=s>SSID</label>
<input id=s name=ssid maxlength=32 placeholder='(blank to keep current network)'>
<label for=p>Password</label>
<input id=p name=password type=password maxlength=64 placeholder='(blank for open networks)'>
)HTML"));

  static const char IPV4_PATTERN_ATTR[] PROGMEM =
    " pattern='^(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)(\\.(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)){3}$' value='";
  bool useStatic = EGPrefs::getBool("net", "static_ip");
  res.sendChunk(F("<details"));
  if (useStatic) res.sendChunk(F(" open"));
  res.sendChunk(F("><summary>Network (advanced)</summary>"
                  "<label style='display:flex;align-items:center;gap:.5em'>"
                  "<input type=checkbox name=static_ip value=1"));
  if (useStatic) res.sendChunk(F(" checked"));
  res.sendChunk(F(">Use static IP (otherwise DHCP)</label>"));

  // head varies per field; pattern + value tail is constant.
  auto ipField = [&](const __FlashStringHelper* head, const char* prefKey) {
    res.sendChunk(head);
    res.sendChunk(FPSTR(IPV4_PATTERN_ATTR));
    res.sendKV(nullptr, EGPrefs::getString("net", prefKey), F("'>"));
  };
  ipField(F("<label for=ip>IP address</label>"
            "<input id=ip name=ip maxlength=15 placeholder='192.168.1.50'"), "ip");
  ipField(F("<label for=gw>Gateway</label>"
            "<input id=gw name=gw maxlength=15 placeholder='192.168.1.1'"), "gw");
  ipField(F("<label for=sn>Subnet mask</label>"
            "<input id=sn name=sn maxlength=15 placeholder='255.255.255.0'"), "sn");
  ipField(F("<label for=dns>DNS server <span class=muted style='font-weight:normal'>(optional)</span></label>"
            "<input id=dns name=dns maxlength=15 placeholder='same as gateway'"), "dns");
  res.sendChunk(F("</details>"));

  res.sendChunk(F(R"HTML(
<button type=submit>Save and reboot</button>
</form>
<p class=muted style='margin-top:1em'>If the new network is unreachable, the previous one is restored automatically after about a minute.</p>
)HTML"));

  // Lazy-load the ~4 KB /ntpjs timezone payload on first expand.
  res.sendChunk(F("<details style='margin-top:1.5em' "
                  "ontoggle=\"if(this.open&&!this.dataset.loaded){"
                  "this.dataset.loaded=1;"
                  "var s=document.createElement('script');"
                  "s.src='/ntpjs" EG_CACHE_BUST "';this.appendChild(s);"
                  "}\">"
                  "<summary>Time / NTP</summary>"));
  ntpclient.renderInlineForm(res);
  res.sendChunk(F("</details>"));

  res.sendChunk(F(R"HTML(
<details style='margin-top:1.5em'><summary>Advanced</summary>
<p class=muted>Erase the saved WiFi credentials and reboot into the captive setup portal.</p>
<form method=GET action=/erase onsubmit="return confirm('Erase saved WiFi credentials? Device will reboot into setup mode.')">
<button type=submit class=danger>Erase WiFi config</button>
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
)HTML"));

  WebPortal::sendPageTail(res);
  res.endChunked();
}

void WebPortal::hWifiScan(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Async poller; returns {state:scanning|complete|failed[,aps:...]}.
  // ?refresh=1 forces a new scan; cache lifetime 60s.
  static uint32_t s_lastScan = 0;
  bool force = req.arg("refresh") != nullptr;

  int status = WiFi.scanComplete();
  if (!force && status == WIFI_SCAN_RUNNING) {
    res.send(200, "application/json", "{\"state\":\"scanning\"}");
    return;
  }

  bool stale = (fast_millis() - s_lastScan) > 60000UL;
  bool needScan = force || status <= 0 || stale;
  if (needScan) {
    if (status != WIFI_SCAN_RUNNING) WiFi.scanDelete();
    WiFi.scanNetworks(true /*async*/, false /*show_hidden*/);
    s_lastScan = fast_millis();
    res.send(200, "application/json", "{\"state\":\"scanning\"}");
    return;
  }

  // Shared dedup/sort/filter pipeline lives in EGPortal so the captive
  // portal and this /wifiscan endpoint never drift apart.
  EGPortal::ScanEntry aps[12];
  size_t count = EGPortal::processScan(aps, 12, status);

  res.beginChunked(200, "application/json");
  res.sendChunk(F("{\"state\":\"complete\",\"aps\":["));
  char row[140];
  for (size_t i = 0; i < count; i++) {
#ifdef ESP8266
    int open = (aps[i].enc == ENC_TYPE_NONE) ? 1 : 0;
#else
    int open = (aps[i].enc == WIFI_AUTH_OPEN) ? 1 : 0;
#endif
    int n = snprintf_P(row, sizeof(row),
      PSTR("%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%d}"),
      i > 0 ? "," : "", aps[i].ssid, (int)aps[i].rssi, open);
    if (n > 0) res.sendChunk(row, (size_t)n);
  }
  res.sendChunk(F("]}"));
  res.endChunked();
}

void WebPortal::hWifiSave(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // arg() returns into a shared static decode buffer; copy each field
  // before the next arg() call (otherwise SSID gets overwritten by the
  // password lookup).
  const char* ssid = req.arg("ssid");
  bool ssidProvided = (ssid && *ssid);
  if (ssidProvided) {
    strncpy(s_pendingWifiSsid, ssid, sizeof(s_pendingWifiSsid) - 1);
    s_pendingWifiSsid[sizeof(s_pendingWifiSsid) - 1] = '\0';

    const char* pass = req.arg("password");
    if (pass) {
      strncpy(s_pendingWifiPass, pass, sizeof(s_pendingWifiPass) - 1);
      s_pendingWifiPass[sizeof(s_pendingWifiPass) - 1] = '\0';
    } else {
      s_pendingWifiPass[0] = '\0';
    }
  }

  // req.arg() returns a shared static buffer - copy before the next call.
  // Always write all four fields so toggling the checkbox keeps values.
  bool wantStatic = (req.arg("static_ip") != nullptr);
  char ip_buf[16], gw_buf[16], sn_buf[16], dns_buf[16];
  auto copy_arg = [&](const char* name, char* dst, size_t cap) {
    const char* v = req.arg(name);
    strncpy(dst, v ? v : "", cap - 1);
    dst[cap - 1] = '\0';
  };
  copy_arg("ip",  ip_buf,  sizeof(ip_buf));
  copy_arg("gw",  gw_buf,  sizeof(gw_buf));
  copy_arg("sn",  sn_buf,  sizeof(sn_buf));
  copy_arg("dns", dns_buf, sizeof(dns_buf));

  // Form-time validate: catch obvious typos (bad IP, gateway off-subnet)
  // before we persist. Same Wifi::validateStatic helper that the boot
  // path uses - guarantees the two checks can't drift.
  if (wantStatic) {
    char err[96];
    if (!Wifi::validateStatic(ip_buf, gw_buf, sn_buf, err, sizeof(err))) {
      char body[160];
      int n = snprintf_P(body, sizeof(body),
        PSTR("Bad network config: %s\nClick Back to fix it.\n"), err);
      if (n > 0) res.send(400, "text/plain", body);
      else       res.send(400, "text/plain", "Bad network config");
      return;
    }
  }

  // Save before overwrite so boot probe-and-revert can roll back.
  Wifi::saveNetBackup();
  EGPrefs::put("net", "static_ip", wantStatic ? "1" : "0");
  EGPrefs::put("net", "ip",  ip_buf);
  EGPrefs::put("net", "gw",  gw_buf);
  EGPrefs::put("net", "sn",  sn_buf);
  EGPrefs::put("net", "dns", dns_buf);
  EGPrefs::commit();

  if (ssidProvided) {
    // Backup the current creds; if new SSID fails on next boot, revert.
    Wifi::saveBackupCreds();
    s_pendingWifiSave = true;
  }

  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Saving WiFi"));
  if (ssidProvided) {
    char body[280];
    int n = snprintf_P(body, sizeof(body),
      PSTR("<p>Saving credentials for <b>%s</b> and rebooting.</p>"
           "<p class=muted>If the new network is reachable, the device will "
           "reconnect on its own. If it isn't, the previous credentials are "
           "restored automatically after about a minute.</p>"),
      s_pendingWifiSsid);
    if (n > 0) res.sendChunk(body, (size_t)n);
  } else {
    res.sendChunk(F("<p>Saving network settings and rebooting.</p>"
                    "<p class=muted>WiFi credentials unchanged. New IP "
                    "configuration will apply once the device comes back up.</p>"));
  }
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();

  // Response must drain before tick() drops the connection via WiFi.begin().
  WebPortal::requestRestart(2500);
}

// ---------- /update (OTA web upload via streaming POST) ----------
// Form POSTs .bin as application/octet-stream; hUpdateBody pipes to
// Update.write(). hUpdateDone fires after BODY_END.
// Device env/chip/ver spliced into window._dev by hUpdateForm.

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
  // Server-side validates entry_addr range and rejects wrong-platform bins
  // before flashing. The env-tag scan is a friendly heads-up only.
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
  x.upload.onprogress=function(v){if(v.lengthComputable)P.value=v.loaded/v.total};
  x.onload=function(){
    if(x.status===200){
      var R='<b style="color:#0a0">'+x.responseText+'</b><br>';
      S.innerHTML=R+'Rebooting&hellip; this page will reload once the device is back.';
      var n=5;
      function pn(){var a=new AbortController();setTimeout(()=>a.abort(),1000);fetch('/ping',{cache:'no-store',signal:a.signal}).then(r=>location.href='/').catch(()=>setTimeout(pn,0))}
      var iv=setInterval(function(){
        if(--n>0)S.innerHTML=R+'Reloading in '+n+'s…';
        else{clearInterval(iv);S.innerHTML=R+'Checking…';pn()}
      },1000);
    }else{
      B.disabled=false;
      S.innerHTML='<b style="color:#c0392b">Upload failed: '+x.responseText+'</b>';
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

// Substring match on BUILD_ENV alone collides with legacy_file paths
// embedding other variant names (e.g. /espgeigerhw.json in HV.h appears
// in every HV build). Match against this delimited tag instead. Must be
// referenced by hUpdateForm below or the linker prunes it - ((used))
// alone doesn't survive --gc-sections.
static const char EG_ENV_TAG[] PROGMEM = "<EGENV:" BUILD_ENV ":ENV>";

void WebPortal::hUpdateForm(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Update"));
  char dev[120];
  int n = snprintf_P(dev, sizeof(dev),
    PSTR("<script>window._dev={env:\"%s\",tag:\"%s\"};</script>"),
    BUILD_ENV, EG_ENV_TAG);
  if (n > 0 && (size_t)n < sizeof(dev)) res.sendChunk(dev, (size_t)n);
  res.sendChunk(FPSTR(UPDATE_FORM_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

// Body handler is called many times per upload; one global because
// EGHttpServer allows only one stream at a time.
static struct {
  size_t expected = 0;     // Content-Length from request
  size_t delivered = 0;    // bytes successfully written via Update.write
  bool   write_error = false;
} s_ota;

void WebPortal::hUpdateBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* data, size_t len, void*) {
  // Body events fire from main-loop tick context, so flash writes via
  // Update.write() are safe here.
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: {
      s_ota.expected    = req.bodyLen();
      s_ota.delivered   = 0;
      s_ota.write_error = false;
      uint32_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(s_ota.expected ? s_ota.expected : maxSketch)) {
        Log::console(PSTR("OTA: Update.begin failed (size=%u, free=%u)"),
                     (unsigned)s_ota.expected, (unsigned)maxSketch);
        s_ota.write_error = true;
      } else {
        Log::console(PSTR("OTA: starting (%u bytes)"), (unsigned)s_ota.expected);
      }
      break;
    }
    case EGHttpServer::BODY_DATA: {
      if (s_ota.write_error || !Update.isRunning()) break;  // already failed; drain
      // First chunk: validate the image header's entry_addr lives in the
      // platform's expected memory range. Both ESP8266 and ESP32 share the
      // 0xE9 magic byte so Update.write alone can't catch wrong-platform
      // bins - which silently bricks the device on next boot.
      if (s_ota.delivered == 0 && len >= 8) {
        uint32_t entry = (uint32_t)data[4]
                       | ((uint32_t)data[5] << 8)
                       | ((uint32_t)data[6] << 16)
                       | ((uint32_t)data[7] << 24);
#ifdef ESP8266
        // ESP8266 IROM0 flash window.
        bool plat_ok = (entry >= 0x40100000UL && entry < 0x40200000UL);
#else
        // ESP32 IRAM startup. S2/S3/C3/C6 variants need their own range.
        bool plat_ok = (entry >= 0x40080000UL && entry < 0x400A0000UL);
#endif
        if (!plat_ok) {
          Log::console(PSTR("OTA: REFUSING wrong-platform bin (entry=0x%08x)"),
                       (unsigned)entry);
          Update.end(false);
          s_ota.write_error = true;
          break;
        }
      }
      size_t wrote = Update.write((uint8_t*)data, len);
      if (wrote != len) {
        Log::console(PSTR("OTA: short write %u/%u err=%u"),
                     (unsigned)wrote, (unsigned)len, Update.getError());
        s_ota.write_error = true;
      } else {
        s_ota.delivered += wrote;
      }
      break;
    }
    case EGHttpServer::BODY_END: {
      // Update.end() with no arg = strict; needs writeCount == _size.
      if (s_ota.write_error || !Update.isRunning()) {
        Log::console(PSTR("OTA: aborting on prior error"));
        if (Update.isRunning()) Update.end(false);  // discard
        s_ota.write_error = true;
        break;
      }
      if (s_ota.delivered != s_ota.expected) {
        Log::console(PSTR("OTA: byte-count mismatch %u != %u"),
                     (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
        Update.end(false);
        s_ota.write_error = true;
        break;
      }
      if (Update.end()) {
        Log::console(PSTR("OTA: success (%u bytes), restarting"),
                     (unsigned)s_ota.delivered);
      } else {
        Log::console(PSTR("OTA: end failed err=%u"), Update.getError());
        s_ota.write_error = true;
      }
      break;
    }
    case EGHttpServer::BODY_ABORT: {
      if (Update.isRunning()) Update.end(false);
      s_ota.write_error = true;
      Log::console(PSTR("OTA: aborted at %u/%u bytes"),
                   (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
      break;
    }
  }
}

void WebPortal::hUpdateDone(EGHttpRequest& req, EGHttpResponse& res, void*) {
  if (s_ota.write_error || Update.hasError()) {
    char errBuf[96];
    snprintf_P(errBuf, sizeof(errBuf),
               PSTR("Update failed: %u/%u bytes, err=%u"),
               (unsigned)s_ota.delivered, (unsigned)s_ota.expected,
               (unsigned)Update.getError());
    res.send(500, "text/plain", errBuf);
    return;
  }
  res.send(200, "text/plain", "OK");
  WebPortal::requestRestart(1500);
}

// ---------- /param (EGPrefs flag-driven config UI) ----------

// HTML-escape, stopping cleanly before an entity would overflow.
static size_t append_escaped(char* dst, size_t cap, size_t pos, const char* src) {
  while (*src && pos + 6 < cap) {
    char c = *src++;
    const char* rep = nullptr;
    switch (c) {
      case '<':  rep = "&lt;";   break;
      case '>':  rep = "&gt;";   break;
      case '&':  rep = "&amp;";  break;
      case '"':  rep = "&quot;"; break;
      case '\'': rep = "&#39;";  break;
    }
    if (rep) { size_t n = strlen(rep); memcpy(dst + pos, rep, n); pos += n; }
    else     { dst[pos++] = c; }
  }
  return pos;
}

#define APPEND_LIT(buf, pos, cap, lit) do {                              \
  size_t _n = sizeof(lit) - 1;                                           \
  if ((pos) + _n < (cap)) { memcpy((buf)+(pos), lit, _n); (pos) += _n; } \
} while (0)

// Heap-alloc'd at BODY_BEGIN, freed in hParam. Zero cost when idle.
static char*  s_paramBody    = nullptr;
static size_t s_paramBodyLen = 0;
static size_t s_paramBodyCap = 0;
static bool   s_paramBodyOk  = false;

void WebPortal::hParamBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                            const char* data, size_t len, void*) {
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: {
      // Free any leftover from a previous aborted save just in case.
      if (s_paramBody) { free(s_paramBody); s_paramBody = nullptr; }
      s_paramBodyLen = 0;
      s_paramBodyCap = 0;
      s_paramBodyOk  = false;
      size_t cap = req.bodyLen();
      if (cap == 0 || cap > 8192) {
        Log::console(PSTR("/param: body size %u out of range"), (unsigned)cap);
        return;
      }
      s_paramBody = (char*)malloc(cap + 1);
      if (!s_paramBody) {
        Log::console(PSTR("/param: malloc(%u) failed"), (unsigned)(cap + 1));
        return;
      }
      s_paramBodyCap = cap;
      s_paramBodyOk  = true;
      break;
    }
    case EGHttpServer::BODY_DATA:
      if (s_paramBodyOk && s_paramBody && s_paramBodyLen + len <= s_paramBodyCap) {
        memcpy(s_paramBody + s_paramBodyLen, data, len);
        s_paramBodyLen += len;
      }
      break;
    case EGHttpServer::BODY_END:
      if (s_paramBody) s_paramBody[s_paramBodyLen] = '\0';
      // hParam (main handler) consumes & frees on the next tick.
      break;
    case EGHttpServer::BODY_ABORT:
      if (s_paramBody) { free(s_paramBody); s_paramBody = nullptr; }
      s_paramBodyLen = 0;
      s_paramBodyCap = 0;
      s_paramBodyOk  = false;
      break;
  }
}

void WebPortal::hParam(EGHttpRequest& req, EGHttpResponse& res, void*) {
  bool did_save = false;
  // Tab from ?tab=N. Default SYSTEM (0). Clamped to the four valid values.
  uint8_t tab = (uint8_t)EGP_CAT_SYSTEM;
  const char* tab_arg = req.arg("t");
  if (tab_arg && tab_arg[0]) {
    int t = atoi(tab_arg);
    if (t >= 0 && t <= 4) tab = (uint8_t)t;
  }

  if (req.method() == EGHttpRequest::POST) {
    // Body lives in the heap buffer accumulated by hParamBody. Use the
    // static decodeArg helper to look up form fields without depending
    // on the slot's body buffer (which is empty for streaming routes).
    const char* body    = s_paramBody;
    size_t      bodyLen = s_paramBodyLen;

    if (s_paramBodyOk && body) {
      char name[64];
      char id_buf[32];
      char val_copy[160];
      for (size_t gi = 0; gi < EGPrefs::group_count(); gi++) {
        const EGPrefGroup* g = EGPrefs::group_at(gi);
        if (!g) continue;
        // Only touch prefs belonging to the tab the form was submitted from.
        // Without this, BOOL fields on other tabs get cleared (absent in body
        // = "off") on every save from another tab.
        if (g->category != tab) continue;
        for (size_t j = 0; j < g->count; j++) {
          const EGPref& p = g->prefs[j];
          if (p.flags & (EGP_HIDDEN | EGP_READONLY)) continue;
          if (p.type == EGP_LABEL || p.type == EGP_HEADER) continue;
          strncpy_P(id_buf, p.id, sizeof(id_buf) - 1);
          id_buf[sizeof(id_buf) - 1] = '\0';
          snprintf_P(name, sizeof(name), PSTR("%s.%s"), g->module_id, id_buf);

          if (p.type == EGP_BOOL) {
            const char* one = EGHttpRequest::decodeArg(body, bodyLen, name) ? "1" : "0";
            EGPrefs::put(g->module_id, id_buf, one);
          } else {
            const char* v = EGHttpRequest::decodeArg(body, bodyLen, name);
            if (!v) continue;
            if (p.flags & EGP_SENSITIVE) {
              if (strcmp(v, "__CLEAR__") == 0) {
                EGPrefs::put(g->module_id, id_buf, "");
                continue;
              }
              if (v[0] == '\0') continue;
            }
            strncpy(val_copy, v, sizeof(val_copy) - 1);
            val_copy[sizeof(val_copy) - 1] = '\0';
            EGPrefs::put(g->module_id, id_buf, val_copy);
          }
        }
      }
    }
    // Buffer consumed - release it back to heap.
    if (s_paramBody) { free(s_paramBody); s_paramBody = nullptr; }
    s_paramBodyLen = 0;
    s_paramBodyCap = 0;
    s_paramBodyOk  = false;

    EGPrefs::commit();
    did_save = true;
    if (EGPrefs::restart_pending()) {
      res.redirect("/restart");
      return;
    }
  }

  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Config"));
  if (did_save) res.sendChunk(F(
    "<div class='a ak'>Saved.</div>"));

  // Tab nav: five links, the current one carries class=ac for styling.
  static const char* const TAB_NAMES[5] = {"System", "Input", "Output", "Upload", "Backup"};
  res.sendChunk(F("<nav class=tabs>"));
  char nav[96];
  for (uint8_t t = 0; t < 5; t++) {
    int nn = snprintf_P(nav, sizeof(nav),
      PSTR("<a href='/param?t=%u'%s>%s</a>"),
      (unsigned)t, t == tab ? " class=ac" : "", TAB_NAMES[t]);
    if (nn > 0) res.sendChunk(nav, (size_t)nn);
  }
  res.sendChunk(F("</nav>"));

  // Backup tab renders the import form only - skip the pref-group rendering.
  if (tab == (uint8_t)EGP_CAT_BACKUP) {
    res.sendChunk(F(
      "<form method=POST action=/import onsubmit=\"return confirm('Replace all config and reboot?')\">"
      "<p><textarea name=blob id=cfgB rows=5 style=\"width:100%;font-family:monospace;font-size:.85em\" placeholder=\"Click Export to read current config, or paste a blob here to import\" required></textarea></p>"
      "<p><button type=button onclick=\"fetch('/export').then(r=>r.text()).then(t=>{var e=document.getElementById('cfgB');e.value=t;e.select();})\">Export</button>"
      " <button type=submit>Import</button></p></form>"
      "<p style=\"color:var(--muted);font-size:.85em\">Import resets all config to defaults then applies the blob. WiFi and network settings are preserved. Device reboots automatically after import.</p>"
      "<p style=\"color:#c0392b;font-size:.85em\"><b>Note:</b> the exported blob contains private information. Keep it private.</p>"));
    WebPortal::sendPageTail(res);
    res.endChunked();
    return;
  }

  char form_open[48];
  int fo = snprintf_P(form_open, sizeof(form_open),
    PSTR("<form method=POST action='/param?t=%u'>"), (unsigned)tab);
  if (fo > 0) res.sendChunk(form_open, (size_t)fo);

  char buf[512];
  int n;

  // Stable-sort visible groups by display_order. Groups with order==0
  // have their own dedicated page (NTP, HV) and are skipped here.
  uint8_t gc = (uint8_t)EGPrefs::group_count();
  uint8_t order[EG_MAX_MODULES];
  if (gc > sizeof(order)) gc = sizeof(order);
  for (uint8_t i = 0; i < gc; i++) order[i] = i;
  for (uint8_t i = 1; i < gc; i++) {
    uint8_t cur = order[i];
    EGModule* m_cur = EGPrefs::module_at(cur);
    uint8_t o_cur = m_cur ? m_cur->display_order() : 50;
    uint8_t j = i;
    while (j > 0) {
      EGModule* m_prev = EGPrefs::module_at(order[j - 1]);
      uint8_t o_prev = m_prev ? m_prev->display_order() : 50;
      if (o_prev <= o_cur) break;
      order[j] = order[j - 1];
      j--;
    }
    order[j] = cur;
  }

  for (uint8_t gi = 0; gi < gc; gi++) {
    EGModule* mod = EGPrefs::module_at(order[gi]);
    if (mod && mod->display_order() == 0) continue;
    const EGPrefGroup* g = EGPrefs::group_at(order[gi]);
    if (!g || g->count == 0) continue;
    if (g->category != tab) continue;

    n = snprintf_P(buf, sizeof(buf), PSTR("<details class=bx><summary>%s</summary>"),
                   g->label ? g->label : g->module_id);
    if (n > 0) res.sendChunk(buf, (size_t)n);

    char s_id[32], s_lbl[80], s_help[120], s_pat[64];
    auto P2S = [](const char* fp, char* dst, size_t cap) -> const char* {
      if (!fp) return "";
      strncpy_P(dst, fp, cap - 1);
      dst[cap - 1] = '\0';
      return dst;
    };
    // Groups that expose lat+lon prefs get a "Find location" picker affordance
    // (opens loc.espgeiger.com in a child window, receives coords via postMessage).
    bool has_lat = false, has_lon = false;

    for (size_t j = 0; j < g->count; j++) {
      const EGPref& p = g->prefs[j];
      if (p.flags & EGP_HIDDEN) continue;

      const char* p_id    = P2S(p.id,      s_id,   sizeof(s_id));
      if (strcmp(p_id, "lat") == 0) has_lat = true;
      else if (strcmp(p_id, "lon") == 0) has_lon = true;
      const char* p_label = P2S(p.label,   s_lbl,  sizeof(s_lbl));
      const char* p_help  = P2S(p.help,    s_help, sizeof(s_help));
      const char* p_pat   = P2S(p.pattern, s_pat,  sizeof(s_pat));

      if (p.type == EGP_LABEL) {
        n = snprintf_P(buf, sizeof(buf), PSTR("<label>%s</label>"), p.label ? p_label : "");
        if (n > 0) res.sendChunk(buf, (size_t)n);
        continue;
      }
      if (p.type == EGP_HEADER) {
        n = snprintf_P(buf, sizeof(buf), PSTR("<h4>%s</h4>"), p.label ? p_label : "");
        if (n > 0) res.sendChunk(buf, (size_t)n);
        continue;
      }

      const char* cur = EGPrefs::getString(g->module_id, p_id);
      const char* mid = g->module_id;
      const char* pid = p_id;
      const char* lbl = p.label ? p_label : p_id;
      bool ro = (p.flags & EGP_READONLY);
      bool inline_bool = (p.type == EGP_BOOL) && (p.flags & EGP_INLINE);
      size_t pos = 0;

      if (!inline_bool) {
        n = snprintf_P(buf, sizeof(buf), PSTR("<label for='%s.%s'>%s</label>"), mid, pid, lbl);
        if (n > 0 && (size_t)n < sizeof(buf)) pos = (size_t)n;
      }

      if (p.type == EGP_BOOL && inline_bool) {
        const char* tip = (p.help && p_help[0]) ? p_help : "";
        n = snprintf_P(buf + pos, sizeof(buf) - pos,
                       PSTR("<label class='il' title='%s'><input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s>%s</label>"),
                       tip, mid, pid, mid, pid,
                       (cur[0] == '1') ? " checked" : "",
                       ro ? " disabled" : "",
                       lbl);
        if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
      } else if (p.type == EGP_BOOL) {
        n = snprintf_P(buf + pos, sizeof(buf) - pos,
                       PSTR("<input type=checkbox id='%s.%s' name='%s.%s' value=1%s%s><br>"),
                       mid, pid, mid, pid,
                       (cur[0] == '1') ? " checked" : "",
                       ro ? " disabled" : "");
        if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
      } else {
        const bool slider = (p.type == EGP_INT || p.type == EGP_UINT)
                         && (p.flags & EGP_SLIDER)
                         && p.min_i != p.max_i;
        const char* itype = "text";
        if (slider) itype = "range";
        else if (p.type == EGP_INT || p.type == EGP_UINT || p.type == EGP_FLOAT) itype = "number";
        else if (p.type == EGP_STRING && (p.flags & EGP_SENSITIVE)) itype = "password";
        else if (p.type == EGP_STRING && (p.flags & EGP_TIME))      itype = "time";

        n = snprintf_P(buf + pos, sizeof(buf) - pos,
                       PSTR("<input type=%s id='%s.%s' name='%s.%s'"),
                       itype, mid, pid, mid, pid);
        if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);

        if (p.flags & EGP_SENSITIVE) {
          const char* ph = cur[0] ? "(unchanged \xE2\x80\x94 leave blank)" : "(not set)";
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" value='' placeholder='%s'"), ph);
          if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
        } else {
          APPEND_LIT(buf, pos, sizeof(buf), " value='");
          pos = append_escaped(buf, sizeof(buf), pos, cur);
          if (pos + 1 < sizeof(buf)) buf[pos++] = '\'';
          if (!slider && cur[0] == '\0'
              && p.default_val && p.default_val[0]
              && !(p.flags & EGP_TIME)) {
            n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" placeholder='%s'"), p.default_val);
            if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
          }
        }
        if (p.type == EGP_STRING && p.max_len > 0) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" maxlength='%u'"), (unsigned)p.max_len);
          if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
        }
        if (p.type == EGP_STRING && p.pattern && p_pat[0]) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" pattern='%s'"), p_pat);
          if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
        }
        if ((p.type == EGP_INT || p.type == EGP_UINT || p.type == EGP_FLOAT) && p.min_i != p.max_i) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR(" min=%ld max=%ld"), (long)p.min_i, (long)p.max_i);
          if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
        }
        if (p.type == EGP_FLOAT) APPEND_LIT(buf, pos, sizeof(buf), " step=any");
        if (!slider && (p.type == EGP_INT || p.type == EGP_UINT)) {
          APPEND_LIT(buf, pos, sizeof(buf), " inputmode=numeric");
        } else if (p.type == EGP_FLOAT) {
          APPEND_LIT(buf, pos, sizeof(buf), " inputmode=decimal");
        }
        if (p.flags & EGP_REQUIRED) APPEND_LIT(buf, pos, sizeof(buf), " required");
        if (ro) APPEND_LIT(buf, pos, sizeof(buf), " readonly");
        if (slider) {
          n = snprintf_P(buf + pos, sizeof(buf) - pos,
                         PSTR(" oninput='this.nextElementSibling.value=this.value'><output>%s</output><br>"), cur);
          if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
        } else {
          APPEND_LIT(buf, pos, sizeof(buf), "><br>");
        }
      }

      if (!inline_bool && p.help && p_help[0]) {
        n = snprintf_P(buf + pos, sizeof(buf) - pos, PSTR("<small>%s</small>"), p_help);
        if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
      }
      if ((p.flags & EGP_SENSITIVE) && cur[0]) {
        n = snprintf_P(buf + pos, sizeof(buf) - pos,
          PSTR("<button type=button class='danger btn-sm' style='margin-top:.2em' "
               "onclick=\"if(confirm('Clear stored value?')){byID('%s.%s').value='__CLEAR__';this.form.submit();}\""
               ">Clear</button>"),
          mid, pid);
        if (n > 0) pos += (size_t)n < (sizeof(buf) - pos) ? (size_t)n : (sizeof(buf) - pos - 1);
      }
      res.sendChunk(buf, pos);
    }
    if (has_lat && has_lon) {
      n = snprintf_P(buf, sizeof(buf), PSTR(
        "<p><button type=button onclick=\"window.open('https://loc.espgeiger.com/','espgeiger-loc')\">Find location</button></p>"
        "<script>window.addEventListener('message',function(e){"
        "if(e.origin!=='https://loc.espgeiger.com')return;"
        "var d=e.data;if(!d||d.type!=='espgeiger-location')return;"
        "var la=byID('%s.lat'),lo=byID('%s.lon');"
        "if(la&&typeof d.lat==='number')la.value=d.lat;"
        "if(lo&&typeof d.lon==='number')lo.value=d.lon;});</script>"),
        g->module_id, g->module_id);
      if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);
    }
    res.sendChunk(F("</details>"));
  }

  res.sendChunk(F(
    "<hr><div id=fErr class='a ae' style=display:none>"
    "Some fields need fixing - check expanded sections.</div>"
    "<button type=submit>Save</button>"
    "<script>document.forms[0].addEventListener('invalid',e=>{"
    "for(let n=e.target;n;n=n.parentNode)n.tagName=='DETAILS'&&(n.open=1);"
    "fErr.style.display=''},true)</script></form>"));
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
  if (strcmp(module_id, "sys") == 0 && strcmp(key, "web_pass") == 0) return true;
  if (strcmp(module_id, "net") == 0) return true;
  if (strcmp(module_id, "wifi") == 0) return true;
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
  // EGBase64 writes 4 chars + NUL terminator (5 bytes); using out
  // directly OOBs when outLen approaches sizeof(out) and clobbers outLen.
  unsigned char tmp[5];
  encode_base64(s.tri, in_len, tmp);
  memcpy(s.out + s.outLen, tmp, 4);
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
  const char* val = EGPrefs::getString(g->module_id, id_buf);
  if (!val) val = "";
  if (p.default_val && strcmp(val, p.default_val) == 0) return false;
  val_out = val;
  return true;
}

// Two passes: count groups for the size-prefix byte, then emit.
static void serializeExportStream(B64Sink& sink) {
  uint8_t group_count = 0;
  for (size_t gi = 0; gi < EGPrefs::group_count(); gi++) {
    const EGPrefGroup* g = EGPrefs::group_at(gi);
    if (!g || g->count == 0) continue;
    for (size_t j = 0; j < g->count; j++) {
      char id_buf[32];
      strncpy_P(id_buf, g->prefs[j].id, sizeof(id_buf) - 1);
      id_buf[sizeof(id_buf) - 1] = '\0';
      const char* val;
      if (is_exportable_pref(g, g->prefs[j], id_buf, val)) {
        group_count++;
        break;
      }
    }
  }

  static const uint8_t MAGIC[3] = {0x11, 0x23, 0xC6};
  b64_write(sink, MAGIC, 3);
  b64_write_u8(sink, 1);
  sink.obf_on = true;
  b64_write_u8(sink, group_count);

  for (size_t gi = 0; gi < EGPrefs::group_count(); gi++) {
    const EGPrefGroup* g = EGPrefs::group_at(gi);
    if (!g || g->count == 0) continue;
    uint8_t emitted_prefs = 0;
    for (size_t j = 0; j < g->count; j++) {
      char id_buf[32];
      strncpy_P(id_buf, g->prefs[j].id, sizeof(id_buf) - 1);
      id_buf[sizeof(id_buf) - 1] = '\0';
      const char* val;
      if (is_exportable_pref(g, g->prefs[j], id_buf, val)) emitted_prefs++;
    }
    if (emitted_prefs == 0) continue;

    size_t mid_len = strlen(g->module_id);
    if (mid_len > 255) continue;
    b64_write_u8(sink, (uint8_t)mid_len);
    b64_write(sink, g->module_id, mid_len);
    b64_write_u8(sink, emitted_prefs);

    for (size_t j = 0; j < g->count; j++) {
      const EGPref& p = g->prefs[j];
      char id_buf[32];
      strncpy_P(id_buf, p.id, sizeof(id_buf) - 1);
      id_buf[sizeof(id_buf) - 1] = '\0';
      const char* val;
      if (!is_exportable_pref(g, p, id_buf, val)) continue;
      size_t klen = strlen(id_buf);
      size_t vlen = strlen(val);
      if (klen > 255 || vlen > 65535) continue;
      b64_write_u8(sink, (uint8_t)klen);
      b64_write(sink, id_buf, klen);
      b64_write_u8(sink, (uint8_t)((vlen >> 8) & 0xFF));
      b64_write_u8(sink, (uint8_t)(vlen & 0xFF));
      b64_write(sink, val, vlen);
    }
  }
}

static void deobfuscate_payload(uint8_t* buf, size_t len) {
  uint32_t st = EXPORT_OBF_SEED;
  for (size_t i = 0; i < len; i++) buf[i] ^= obf_step(&st);
}

static const char* validateImport(uint8_t* buf, size_t len) {
  if (len < 10) return "too short";
  if (buf[0] != 0x11 || buf[1] != 0x23 || buf[2] != 0xC6) return "bad magic";
  if (buf[3] != 1) return "bad format";
  size_t payload_len = len - 4;
  uint32_t expected = ((uint32_t)buf[payload_len    ] << 24) |
                      ((uint32_t)buf[payload_len + 1] << 16) |
                      ((uint32_t)buf[payload_len + 2] <<  8) |
                      ((uint32_t)buf[payload_len + 3]      );
  uint32_t actual = crc32_update(0, buf, payload_len) ^ EXPORT_OBF_SEED;
  if (expected != actual) return "checksum mismatch";

  deobfuscate_payload(buf + 4, payload_len - 4);

  size_t off = 4;
  uint8_t gcount = buf[off++];
  for (uint8_t gi = 0; gi < gcount; gi++) {
    if (off + 1 > payload_len) return "truncated group hdr";
    uint8_t mid_len = buf[off++];
    if (mid_len >= 32 || off + mid_len + 1 > payload_len) return "bad group hdr";
    off += mid_len;
    uint8_t pcount = buf[off++];
    for (uint8_t pi = 0; pi < pcount; pi++) {
      if (off + 3 > payload_len) return "truncated pref";
      uint8_t klen = buf[off++];
      if (klen >= 32 || off + klen + 2 > payload_len) return "bad pref key";
      off += klen;
      uint16_t vlen = ((uint16_t)buf[off] << 8) | buf[off + 1];
      off += 2;
      if (off + vlen > payload_len) return "truncated pref value";
      off += vlen;
    }
  }
  return nullptr;
}

// Reset-then-apply: wipe all user prefs to defaults (net group + sys.web_pass
// preserved by reset_all(keep_network=true)), then overlay blob values on top.
// Silent commit avoids the on_prefs_saved cascade that would WDT during bulk
// apply; restart on success re-fires callbacks cleanly on next boot.
// Returns count applied, or -1 on parse error (err_out gets a UI string).
static int applyImport(uint8_t* buf, size_t len, const char** err_out) {
  const char* err = validateImport(buf, len);
  if (err) { if (err_out) *err_out = err; return -1; }

  EGPrefs::reset_all(/*keep_network=*/true);

  size_t payload_len = len - 4;   // CRC stripped
  size_t off = 4;
  uint8_t gcount = buf[off++];
  int applied = 0;
  (void)payload_len;
  for (uint8_t gi = 0; gi < gcount; gi++) {
    uint8_t mid_len = buf[off++];
    char module_id[32];
    memcpy(module_id, buf + off, mid_len); off += mid_len;
    module_id[mid_len] = '\0';
    uint8_t pcount = buf[off++];
    for (uint8_t pi = 0; pi < pcount; pi++) {
      uint8_t klen = buf[off++];
      char key[32];
      memcpy(key, buf + off, klen); off += klen;
      key[klen] = '\0';
      uint16_t vlen = ((uint16_t)buf[off] << 8) | buf[off + 1];
      off += 2;
      char val[256];
      if (vlen >= sizeof(val)) { off += vlen; continue; }
      memcpy(val, buf + off, vlen); off += vlen;
      val[vlen] = '\0';
      if (is_skip_pref(module_id, key)) continue;
      // Forward-compat: skip unknown keys (older blob, newer firmware).
      bool known = false;
      for (size_t gi2 = 0; gi2 < EGPrefs::group_count() && !known; gi2++) {
        const EGPrefGroup* g = EGPrefs::group_at(gi2);
        if (!g || strcmp(g->module_id, module_id) != 0) continue;
        for (size_t j = 0; j < g->count; j++) {
          char id_buf[32];
          strncpy_P(id_buf, g->prefs[j].id, sizeof(id_buf) - 1);
          id_buf[sizeof(id_buf) - 1] = '\0';
          if (strcmp(id_buf, key) == 0) { known = true; break; }
        }
      }
      if (!known) continue;
      EGPrefs::put(module_id, key, val);
      applied++;
    }
  }
  EGPrefs::commit(false);   // Silent: caller is about to restart, callbacks re-fire on boot.
  return applied;
}

void WebPortal::hExport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  // Zero heap allocation. Sink lives on stack (~270 B); serializer reads
  // EGPrefs shadow directly and streams base64-encoded chunks to res.
  res.beginChunked(200, "text/plain");
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
  // Write CRC bytes WITHOUT folding them into the running CRC (we'd be
  // hashing our own hash). Bypass b64_write's crc update.
  for (int i = 0; i < 4; i++) {
    sink.tri[sink.triLen++] = crc_be[i];
    if (sink.triLen == 3) b64_emit_quartet(sink, 3);
  }
  b64_finish(sink);
  res.endChunked();
}

// Streaming-body storage for /import. Same shape as hParamBody.
static char*  s_importBody    = nullptr;
static size_t s_importBodyLen = 0;
static size_t s_importBodyCap = 0;
static bool   s_importBodyOk  = false;

void WebPortal::hImportBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* data, size_t len, void*) {
  switch (ev) {
    case EGHttpServer::BODY_BEGIN: {
      if (s_importBody) { free(s_importBody); s_importBody = nullptr; }
      s_importBodyLen = 0;
      s_importBodyCap = 0;
      s_importBodyOk  = false;
      size_t cap = req.bodyLen();
      if (cap == 0 || cap > 8192) return;
      s_importBody = (char*)malloc(cap + 1);
      if (!s_importBody) return;
      s_importBodyCap = cap;
      s_importBodyOk  = true;
      break;
    }
    case EGHttpServer::BODY_DATA:
      if (s_importBodyOk && s_importBody && s_importBodyLen + len <= s_importBodyCap) {
        memcpy(s_importBody + s_importBodyLen, data, len);
        s_importBodyLen += len;
      }
      break;
    case EGHttpServer::BODY_END:
      if (s_importBody) s_importBody[s_importBodyLen] = '\0';
      break;
    case EGHttpServer::BODY_ABORT:
      if (s_importBody) { free(s_importBody); s_importBody = nullptr; }
      s_importBodyLen = 0;
      s_importBodyCap = 0;
      s_importBodyOk  = false;
      break;
  }
}

// Inline url-decode of form body for `blob=...` only. Mutates buf to
// place the decoded value in-place; returns pointer to it (NUL-terminated).
// Avoids EGHttpRequest::decodeArg's 128-byte static buffer which truncates
// our multi-KB blob.
static char* extract_blob_field(char* body, size_t bodyLen, size_t* out_len) {
  if (bodyLen < 5) return nullptr;
  char* eq = nullptr;
  for (size_t i = 0; i + 5 <= bodyLen; i++) {
    bool at_start = (i == 0 || body[i - 1] == '&');
    if (at_start && memcmp(body + i, "blob=", 5) == 0) {
      eq = body + i + 5;
      break;
    }
  }
  if (!eq) return nullptr;
  char* src = eq;
  char* dst = eq;
  char* end = body + bodyLen;
  auto hex_nyb = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
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
  *dst = '\0';
  if (out_len) *out_len = dst - eq;
  return eq;
}

void WebPortal::hImport(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  auto renderError = [&](const char* msg) {
    res.beginChunked(200, "text/html");
    WebPortal::sendPageHead(res, F("Import"));
    char buf[160];
    snprintf_P(buf, sizeof(buf),
               PSTR("<p>Import failed: %s.</p><p><a href=/param>Back</a></p>"), msg);
    res.sendChunk(buf);
    WebPortal::sendPageTail(res);
    res.endChunked();
    if (s_importBody) { free(s_importBody); s_importBody = nullptr; }
  };
  if (!s_importBodyOk || !s_importBody || s_importBodyLen == 0) {
    renderError("no body");
    return;
  }
  size_t b64_len = 0;
  char* b64 = extract_blob_field(s_importBody, s_importBodyLen, &b64_len);
  if (!b64 || b64_len == 0) {
    renderError("missing blob");
    return;
  }
  unsigned int raw_cap = decode_base64_length((unsigned char*)b64, b64_len);
  if (raw_cap == 0 || raw_cap > 8192) {
    renderError("bad base64");
    return;
  }
  uint8_t* raw = (uint8_t*)malloc(raw_cap);
  if (!raw) {
    renderError("alloc failed");
    return;
  }
  unsigned int raw_len = decode_base64((unsigned char*)b64, b64_len, raw);
  free(s_importBody); s_importBody = nullptr;
  const char* err = nullptr;
  int applied = applyImport(raw, raw_len, &err);
  free(raw);
  if (applied < 0) {
    renderError(err ? err : "bad blob");
    return;
  }
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Imported"));
  char msg[96];
  snprintf_P(msg, sizeof(msg),
             PSTR("<p>Imported %d prefs. Restarting.</p>"), applied);
  res.sendChunk(msg);
  WebPortal::sendRestartCountdown(res);
  WebPortal::sendPageTail(res);
  res.endChunked();
  WebPortal::requestRestart(1500);
}

// ---------- /status + /js + /cs ----------

// Body for /status. Canvas gets a fixed light background so picograph's
// default-black axis text and stock trace colors (red/green/blue) stay
// readable in both light and dark mode - picographJS doesn't know about
// the page theme. Console textarea uses var(--bg)/--fg so it does.
static const char STATUS_BODY[] PROGMEM = R"HTML(
<style>
#blip{display:inline-block;width:.7em;height:.7em;border-radius:50%;background:var(--border);margin-right:.5em;vertical-align:middle;transition:background-color .08s ease-out,box-shadow .08s ease-out}
#blip.f{background:var(--blip,#0f0);box-shadow:0 0 6px var(--blip,#0f0)}
#snd{display:none;width:auto;padding:.2em .8em;font-size:.85em;float:right;margin:0 0 .5em .5em}
:root.crt #snd{display:inline-block}
</style>
<button id=snd>Sound: off</button>
<canvas id=g1></canvas>
<div id=g2></div>
<div class=stat>
<div class=row><b>CPM</b><span><span id=blip></span><span id=cpm>-</span></span><b>CPS</b><span id=cs>-</span></div>
<div class=row><b><span class=usvL>&micro;Sv/h</span></b><span id=usv class=usv>-</span><b>Total clicks</b><span id=tc>-</span></div>
<div class=row><b>Uptime</b><span id=upt>-</span><b>Signal</b><span id=rssi>-</span></div>
<div class=row id=envR style=display:none></div>
</div>
<h2>Console</h2>
<textarea readonly id=t1 wrap=off></textarea>
<details style="margin-top:.4em"><summary style="cursor:pointer;color:var(--muted);width:1.5em;list-style-position:inside">&nbsp;</summary>
<form id=cf style="display:flex;gap:.5em;margin-top:.4em" onsubmit="return cmdSend(event)">
<input id=ci placeholder="cmd" style="flex:1;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,'Liberation Mono',monospace;font-size:.95em" autocomplete=off spellcheck=false>
<button type=submit style="width:auto">Send</button>
</form>
</details>
<script>function cmdSend(e){e.preventDefault();var i=byID('ci'),v=i.value.trim();if(!v)return!1;var h=2166136261;for(var k=0;k<v.length;k++)h=Math.imul(h^v.charCodeAt(k),16777619)>>>0;if(h==1532375204)byID('snd').click();else if(h==4098154876)toggleCrt();else fetch('/webcmd',{method:'POST',body:v});i.value='';return!1}</script>
<script src=/js)HTML" EG_CACHE_BUST R"HTML(></script>
)HTML";

void WebPortal::hStatus(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  const char* fn = DeviceInfo::friendlyName();
  const char* sub = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  WebPortal::sendPageHead(res, F("Status"), sub);

  char chipscript[64];
  int csn = snprintf_P(chipscript, sizeof(chipscript),
                       PSTR("<script>window.CHIPID=\"%s\"</script>"),
                       DeviceInfo::chipid());
  if (csn > 0 && (size_t)csn < sizeof(chipscript)) res.sendChunk(chipscript, (size_t)csn);

  char seedBuf[240];
  size_t sp = 0;
  int sn = snprintf_P(seedBuf, sizeof(seedBuf), PSTR("<script>window._seedHist=["));
  if (sn > 0 && (size_t)sn < sizeof(seedBuf)) sp += (size_t)sn;
  int histSize = gcounter.cpm_history.size();
  bool first = true;
  for (int i = 0; i < histSize && sp < sizeof(seedBuf) - 16; i += 3) {
    sn = snprintf_P(seedBuf + sp, sizeof(seedBuf) - sp,
                    first ? PSTR("%d") : PSTR(",%d"),
                    gcounter.cpm_history[i]);
    if (sn > 0 && (size_t)sn < sizeof(seedBuf) - sp) sp += (size_t)sn;
    first = false;
  }
  sn = snprintf_P(seedBuf + sp, sizeof(seedBuf) - sp, PSTR("]</script>"));
  if (sn > 0 && (size_t)sn < sizeof(seedBuf) - sp) sp += (size_t)sn;
  res.sendChunk(seedBuf, sp);

  res.sendChunk(FPSTR(STATUS_BODY));
  // Status page wants back link first, then the ESPGeiger credit.
  res.sendChunk(F("<p class=back-row><a class=back href=/>\xe2\x86\x90 Home</a></p>"));
  WebPortal::sendPageFooter(res);
  res.sendChunk(F("</body></html>"));
  res.endChunked();
}

void WebPortal::hJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", CACHE_IMMUTABLE);
#if EG_GZ_JS_BUNDLE
  res.sendGzipP(200, "application/javascript", JS_BUNDLE_GZ, JS_BUNDLE_GZ_LEN);
#else
  res.beginChunked(200, "application/javascript");
  res.sendChunk(FPSTR(picographJS));
  res.sendChunk(FPSTR(statusJS));
  res.endChunked();
#endif
}

void WebPortal::hOutputs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Aggregates each module's status_json fragment into one object.
  // Several hundred bytes once MQTT/WebAPI/etc all chime in.
  char buf[512];
  size_t pos = 0;
  buf[pos++] = '{';
  pos += EGModuleRegistry::collect_status_json(buf + pos, sizeof(buf) - pos - 2, fast_millis());
  buf[pos++] = '}';
  res.beginChunked(200, "application/json");
  res.sendChunk(buf, pos);
  res.endChunked();
}

// JSON-escape a value into out. Returns chars written. Used by /vars.json
// because friendly name etc could contain quotes / backslashes / control
// chars and break the wire.
static size_t json_escape(const char* in, size_t in_len, char* out, size_t cap) {
  size_t pos = 0;
  for (size_t i = 0; i < in_len; i++) {
    char c = in[i];
    if (pos + 6 >= cap) break;   // worst case \u00XX = 6 chars
    if (c == '"' || c == '\\') { out[pos++] = '\\'; out[pos++] = c; }
    else if (c == '\n')        { out[pos++] = '\\'; out[pos++] = 'n'; }
    else if (c == '\r')        { out[pos++] = '\\'; out[pos++] = 'r'; }
    else if (c == '\t')        { out[pos++] = '\\'; out[pos++] = 't'; }
    else if ((uint8_t)c < 0x20) { /* drop other controls */ }
    else                       { out[pos++] = c; }
  }
  return pos;
}

void WebPortal::hVars(EGHttpRequest& req, EGHttpResponse& res, void*) {
  (void)req;
  // Walks every variable the template engine knows about and dumps its
  // current rendered value. Useful for authoring sout.tpl templates.
  res.beginChunked(200, "application/json");
  res.sendChunk(F("{"));
  size_t total = OutputVars::count();
  bool first = true;
  for (size_t i = 0; i < total; i++) {
    OutputVars::VarDef v = OutputVars::at(i);
    char raw[64];
    size_t n = OutputVars::render(v.key, strlen(v.key), raw, sizeof(raw));
    // Renderers return 0 when the underlying hardware is absent (humidity
    // on BMP280, env vars when no sensor, etc). Omit the key entirely so
    // /vars.json reflects what's actually queryable on this device.
    if (n == 0) continue;
    char esc[80];
    size_t en = json_escape(raw, n, esc, sizeof(esc));
    char line[128];
    int m = snprintf_P(line, sizeof(line), PSTR("%s\"%s\":\"%.*s\""),
                       first ? "" : ",", v.key, (int)en, esc);
    if (m > 0 && (size_t)m < sizeof(line)) res.sendChunk(line, (size_t)m);
    first = false;
  }
  res.sendChunk(F("}"));
  res.endChunked();
}

void WebPortal::hMetrics(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Prometheus text exposition. chipid is the only stable per-series label.
  // Friendly name + build metadata are exposed via device_info so renaming
  // a device doesn't fork its historical time series.
  const char* cid  = DeviceInfo::chipid();
  const char* fn   = DeviceInfo::friendlyName();
  const char* name = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  float ratio  = gcounter.get_ratio();
  float cpm    = gcounter.get_cpmf();
  float usv_h  = ratio > 0 ? cpm / ratio : 0.0f;
  uint64_t lifeRaw = gcounter.get_lifetime_clicks_total();
  unsigned long life = (lifeRaw > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (unsigned long)lifeRaw;
  res.beginChunked(200, "text/plain; version=0.0.4");
  char buf[320];
  char fb[24];
  int n;
  // Emit HELP+TYPE for a metric name (call once per name).
  #define HEAD(mname, mtype, mhelp) \
    res.sendChunk(F("# HELP " mname " " mhelp "\n# TYPE " mname " " mtype "\n"))
  // Emit a single value line. `extra` lets us add extra labels (eg window).
  #define VAL(mname, extra, fmt, val) \
    n = snprintf_P(buf, sizeof(buf), \
      PSTR(mname "{chipid=\"%s\"" extra "} " fmt "\n"), \
      cid, val); \
    if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n)

  HEAD("device_info", "gauge",
       "Device metadata; value always 1. Join via on(chipid) for naming.");
  n = snprintf_P(buf, sizeof(buf),
    PSTR("device_info{chipid=\"%s\",name=\"%s\",model=\"%s\",ver=\"%s\",env=\"%s\"} 1\n"),
    cid, name, GEIGER_MODEL, RELEASE_VERSION, BUILD_ENV);
  if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);

  HEAD("geiger_cpm", "gauge", "Counts per minute (window label selects averaging period)");
  format_f(fb, sizeof(fb), cpm, 2);                   VAL ("geiger_cpm", ",window=\"1m\"",  "%s", fb);
  format_f(fb, sizeof(fb), gcounter.get_cpm5f(), 2);  VAL ("geiger_cpm", ",window=\"5m\"",  "%s", fb);
  format_f(fb, sizeof(fb), gcounter.get_cpm15f(), 2); VAL ("geiger_cpm", ",window=\"15m\"", "%s", fb);

  HEAD("geiger_cps", "gauge", "Instantaneous counts per second");
  format_f(fb, sizeof(fb), gcounter.get_cps(), 2);    VAL ("geiger_cps", "", "%s", fb);

  HEAD("geiger_usv_per_hour", "gauge", "Dose rate in microsieverts per hour");
  format_f(fb, sizeof(fb), usv_h, 4);                 VAL ("geiger_usv_per_hour", "", "%s", fb);

  HEAD("geiger_lifetime_clicks_total", "counter", "Total clicks counted over device life");
  VAL ("geiger_lifetime_clicks_total", "", "%lu", life);

  HEAD("device_uptime_seconds", "gauge", "Seconds since boot");
  VAL ("device_uptime_seconds", "", "%lu", (unsigned long)DeviceInfo::uptime());

  HEAD("device_free_heap_bytes", "gauge", "Free heap memory");
  VAL ("device_free_heap_bytes", "", "%u", (unsigned)DeviceInfo::freeHeap());

  HEAD("device_wifi_rssi_dbm", "gauge", "WiFi signal strength");
  VAL ("device_wifi_rssi_dbm", "", "%d", (int)Wifi::rssi);

  // Inter-pulse-interval histogram - cumulative Prometheus 'le' buckets.
  // Bucket b upper bound = (64us << b); top bucket saturates as +Inf.
  HEAD("geiger_pulse_interval_seconds", "histogram",
       "Inter-pulse intervals in seconds, log2 buckets");
  const uint32_t* hist = gcounter.pulse_histogram();
  const uint8_t   nb   = Counter::pulse_histogram_buckets();
  uint32_t cumul = 0;
  for (uint8_t b = 0; b < nb; b++) {
    cumul += hist[b];
    if (b < nb - 1) {
      uint32_t ub_us = 64UL << b;  // bucket upper bound in microseconds
      n = snprintf_P(buf, sizeof(buf),
        PSTR("geiger_pulse_interval_seconds_bucket{chipid=\"%s\",le=\"%lu.%06lu\"} %lu\n"),
        cid,
        (unsigned long)(ub_us / 1000000UL),
        (unsigned long)(ub_us % 1000000UL),
        (unsigned long)cumul);
    } else {
      n = snprintf_P(buf, sizeof(buf),
        PSTR("geiger_pulse_interval_seconds_bucket{chipid=\"%s\",le=\"+Inf\"} %lu\n"),
        cid, (unsigned long)cumul);
    }
    if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);
  }
  n = snprintf_P(buf, sizeof(buf),
    PSTR("geiger_pulse_interval_seconds_count{chipid=\"%s\"} %lu\n"),
    cid, (unsigned long)cumul);
  if (n > 0 && (size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);

  #undef HEAD
  #undef VAL
  res.endChunked();
}

void WebPortal::hWebCmd(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Raw command line (text/plain). Trim trailing CR/LF before dispatch.
  const char* body = req.body();
  if (!body || !*body) {
    res.send(400, "text/plain", "empty command");
    return;
  }
  char line[96];
  size_t n = 0;
  while (body[n] && n < sizeof(line) - 1 && body[n] != '\r' && body[n] != '\n') {
    line[n] = body[n];
    n++;
  }
  line[n] = '\0';
  serialcmd.dispatch(line);
  res.send(200, "text/plain", "ok");
}

void WebPortal::hConsoleStream(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Cursor is the log index the client last saw. Returns a single
  // delivery: "<cur_idx>,<tz_offset_min>\n" + any new log lines since.
  // Empty body (just the header line) when caller is already caught up.
  uint32_t counter = 0;
  const char* c1 = req.arg("c1");
  if (c1 && *c1) counter = (uint32_t)atoi(c1);

  res.addHeader("Cache-Control", "no-store");
  res.beginChunked(200, "text/plain");

  char hdr[24];
  int n = snprintf_P(hdr, sizeof(hdr), PSTR("%u,%d\n"),
                     (unsigned)(uint8_t)Log::getLogIdx(),
                     ntpclient.tz_offset_min);
  if (n > 0) res.sendChunk(hdr, (size_t)n);

  uint8_t curIdx = (uint8_t)Log::getLogIdx();
  if (counter != curIdx) {
    if (counter == 0) counter = curIdx;
    do {
      char* tmp;
      size_t len;
      Log::getLog(counter, &tmp, &len);
      if (len) {
        char line[256];
        size_t copyLen = (len < sizeof(line) - 1) ? len : sizeof(line) - 1;
        memcpy(line, tmp, copyLen);
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

// statusJS - cpm-graph poller (hits /json) + console poller (hits /cs).
// Single canonical copy; legacy ConfigManager /js externs this same symbol
// (see html.h).
#if !EG_GZ_JS_BUNDLE
extern const char statusJS[] PROGMEM = R"JS(
!function(){var $=byID,B=$('blip'),U=$('upt'),C=$('cpm'),T=$('tc'),V=$('usv'),S=$('cs'),R=$('rssi'),D=Date,X=XMLHttpRequest,O=setTimeout,P=n=>String(n).padStart(2,"0"),e=new Graph("g1",["CPM","CPM5","CPM15"],"cpm","g2",15,null,0,!0,!0,5,5);
var ac,cps=0,nb,mu=$('snd'),mt=1,AC=window.AudioContext||window.webkitAudioContext;
var LS=localStorage;
function applySnd(){var s=LS.sndBtn,c=document.documentElement.classList.contains('crt');mu.style.display=(s==='1'||((window.xd||c)&&s!=='0'))?'':'none'}
applySnd();window.applySnd=applySnd;
function ensureAC(){if(!mt&&!ac&&AC){ac=new AC();var N=Math.floor(ac.sampleRate*.008);nb=ac.createBuffer(1,N,ac.sampleRate);var d=nb.getChannelData(0),sd=2166136261>>>0,cid=window.CHIPID||'0';for(var i=0;i<cid.length;i++)sd=Math.imul(sd^cid.charCodeAt(i),16777619)>>>0;if(!sd)sd=1;for(var i=0;i<N;i++){sd^=sd<<13;sd^=sd>>>17;sd^=sd<<5;d[i]=((sd&65535)/32768-1)*Math.exp(-i*.04)}}if(ac&&ac.resume)ac.resume()}
mu.onclick=function(){mt=!mt;mu.textContent='Sound: '+(mt?'off':'on');ensureAC()};
window.toggleTick=()=>{LS.sndBtn=LS.sndBtn==='1'?'0':'1';applySnd()};
(function clk(){var n=500;
if(cps>0&&!mt&&ac){
var t=ac.currentTime,s=ac.createBufferSource(),f=ac.createBiquadFilter(),g=ac.createGain();
s.buffer=nb;f.type='bandpass';f.frequency.value=1200;f.Q.value=2;g.gain.value=.7;
s.connect(f);f.connect(g);g.connect(ac.destination);s.start(t);
n=Math.max(20,-Math.log(1-Math.random())/cps*1000)}
O(clk,n)})();
if(window._seedHist&&window._seedHist.length){var N=window._seedHist.length,w=D.now();window._seedHist.forEach((v,i)=>e.update([v,v,v],new D(w-(N-1-i)*3e3).toLocaleTimeString()))}
var J,L=0,I=2e3,K='#0f0',A=0,F=function(){B.classList.add('f');O(()=>B.classList.remove('f'),90)};
B.style.setProperty('--blip',K);
setInterval(function(){A+=100/I;if(A>=1){F();if((A-=1)>3)A=3}},100);
var Q=function(){if(!window._csk){window._csk=1;O(f,100)}},t=function(){var n=new X;n.open("GET","/s",!0);
n.onload=function(){if(n.status>=200&&n.status<400){var o=JSON.parse(n.responseText),u=o.ut;
U.textContent=(u/86400|0)+"T"+P((u/3600|0)%24)+":"+P((u/60|0)%60)+":"+P(u%60);
C.textContent=o.c.toFixed(2);T.textContent=o.tc;setUsv(V,o.c/o.r);S.textContent=o.cs.toFixed(2);cps=o.cs;
var v=o.rssi,p=v<=-100?0:v>=-50?100:2*(v+100);R.textContent=v+' dBm ('+p+'%)';
if(o.t!=null||o.h!=null||o.p!=null){var tu=o.tu|0,us=['\xb0C','\xb0F','K'],ps=[];if(o.t!=null)ps.push(['Temp',o.t.toFixed(1)+us[tu]]);if(o.h!=null)ps.push(['Humid',o.h.toFixed(0)+'%']);if(o.p!=null)ps.push(['Press',o.p.toFixed(1)+' hPa']);var fx=ps.length>2;$('envR').className=fx?'row envflex':'row';$('envR').innerHTML=ps.map(fx?function(p){return '<div><b>'+p[0]+'</b>'+p[1]+'</div>'}:function(p){return '<b>'+p[0]+'</b><span>'+p[1]+'</span>'}).join('');$('envR').style.display=''}else $('envR').style.display='none';
e.update([o.c,o.c5,o.c15]);var r=o.c5>0&&o.c>0?o.c/o.c5:1;
I=Math.max(100,Math.min(4e3,2e3/r));
var z=(o.c-o.c5)/Math.sqrt(Math.max(o.c5,1e-6));
K=o.c<.01&&o.c5<.01?'#08f':z>3?'#f33':z>1.5?'#fa0':z<-1.5?'#c0f':'#0f0';
B.style.setProperty('--blip',K)}L=D.now();Q();J=O(t,3e3)};
n.onerror=function(){L=D.now();Q();J=O(t,6e3)};n.send()};
O(t,250);document.addEventListener("visibilitychange",function(){if(!document.hidden){clearTimeout(J);J=O(t,Math.max(0,3e3-(D.now()-L)))}})}();
var DD=Date,lt,lc=0,x,id=0,M=1e4,dl=200,dz=0,T1,ci=3e3;
function f(){clearTimeout(lt);T1=T1||byID("t1");x=new XMLHttpRequest;
x.onload=function(){if(200==x.status){var n=x.responseText,nl=n.indexOf("\n"),ab=(T1.scrollHeight-T1.scrollTop-T1.clientHeight)<50,h=n.substr(0,nl).split(",");
id=h[0];dz=+h[1]||0;var e=n.substr(nl+1);
if(e){e=e.replace(/^(\d\d):(\d\d):(\d\d)(?!\*)/gm,function(_,h,m,s){
var b=-new DD().getTimezoneOffset(),T=((+h*60+(+m)+b-dz)%1440+1440)%1440;
return String(T/60|0).padStart(2,"0")+":"+String(T%60).padStart(2,"0")+":"+s});
T1.value+=e;var L=T1.value.split("\n");if(L.length>M)T1.value=L.slice(-(M-dl)).join("\n")}
if(ab)T1.scrollTop=T1.scrollHeight}lc=DD.now();ci=e?3e3:Math.min(ci*1.5,8.3e3);lt=setTimeout(f,ci)};
x.onerror=function(){lc=DD.now();lt=setTimeout(f,6e3)};x.open("GET","/cs?c1="+id,!0);x.send();return!1}
document.addEventListener("visibilitychange",function(){if(!document.hidden){if(lc&&DD.now()-lc>1.08e7)id=0;ci=3e3;clearTimeout(lt);lt=setTimeout(f,Math.max(0,3e3-(DD.now()-lc)))}});
)JS";
#endif

// picographJS - shared graphing library used by /hvjs (HV) and /js
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
update(t,ts){this.uP(t),this.uL(t),this.uT(ts),this.rd()}
})JS";
#endif
