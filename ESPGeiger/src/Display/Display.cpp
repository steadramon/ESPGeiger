/*
  Display.cpp - display module

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
#include "Display.h"
#include "App/AppKeys.h"
#ifdef SSD1306_DISPLAY
#include "DisplayType.h"
#include "screenJS.gz.h"
#include "Corium/Module/Registry.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Output/QuietWindow.h"
#include "Corium/Output/LedSignal.h"
#include "App/LedPatterns.h"
#include "App/Notice.h"
#include "Corium/WebPortal/WebPortal.h"
#include "Counter/Counter.h"
#include <EGHttpServer.h>

Display display;
CORIUM_REGISTER_MODULE(display)

CORIUM_PSTR(K_RST,      "rst");
CORIUM_PSTR(K_FLIP,     "flip");
CORIUM_PSTR(K_TIMEOUT,  "timeout");
CORIUM_PSTR(K_ON_TIME,  "on_time");
CORIUM_PSTR(K_OFF_TIME, "off_time");

#ifdef GEIGER_PUSHBUTTON
CORIUM_PSTR(DS_L_TMO, "Idle time (s)");
CORIUM_PSTR(DS_H_TMO, "Blank after this long idle, 0 = use the on/off times");
#endif
CORIUM_PSTR(DS_L_ONT, "On time");
CORIUM_PSTR(DS_L_OFT, "Off time");
#ifdef GEIGER_PUSHBUTTON
CORIUM_PSTR(DS_H_ONT, "Display on (used when idle time = 0)");
CORIUM_PSTR(DS_H_OFT, "Display off (used when idle time = 0)");
#else
CORIUM_PSTR(DS_H_ONT, "Display on");
CORIUM_PSTR(DS_H_OFT, "Display off");
#endif
CORIUM_PSTR(DS_L_FLP, "Flip 180\xC2\xB0");
#if DISP_MANY
CORIUM_PSTR(DS_L_TYP, "Display type");
// One slot per type up to the last one built. Blanks keep the numbers in place, 127 max
static const char DS_O_TYP[] PROGMEM =
#if DISP_HAS(0)
  "SSD1306"
#endif
#if DISPLAY_TYPES >> 1
  "|"
#endif
#if DISP_HAS(1) && DISP_HAS_TINY
  "SH1106"
#elif DISP_HAS(1)
  "SH1106 (1.3in)"
#endif
#if DISPLAY_TYPES >> 2
  "|"
#endif
#if DISP_HAS(2)
  "SSD1309"
#endif
#if DISPLAY_TYPES >> 3
  "|"
#endif
#if DISP_HAS(3)
  "Tiny SSD1306"
#endif
#if DISPLAY_TYPES >> 4
  "|"
#endif
#if DISP_HAS(4)
  "Tiny SH1106"
#endif
#if DISPLAY_TYPES >> 5
  "|"
#endif
#if DISP_HAS(5)
  "7-Seg (MAX7219)"
#endif
#if DISPLAY_TYPES >> 6
  "|"
#endif
#if DISP_HAS(6)
  "Round (Mottram MAX7219)"
#endif
#if DISPLAY_TYPES >> 7
  "|"
#endif
#if DISP_HAS(7)
  "Matrix (MAX7219)"
#endif
#if DISPLAY_TYPES >> 8
  "|"
#endif
#if DISP_HAS(8)
  "7-Seg (TM1637)"
#endif
#if DISPLAY_TYPES >> 9
  "|"
#endif
#if DISP_HAS(9)
  "128x32"
#endif
  "";
static_assert(sizeof(DS_O_TYP) - 1 <= CORIUM_PREF_PATTERN_MAX, "display types past the options buffer");
static_assert(DISPLAY_TYPES >> 10 == 0, "no display.type past 9");
// highest type number built in
static constexpr uint8_t top_type(uint32_t m) { return m > 1 ? 1 + top_type(m >> 1) : 0; }
#define DS_TYPE_MAX top_type(DISPLAY_TYPES)
#endif
#ifndef OLED_PINS_BLOCKED
CORIUM_PSTR(DS_L_RST, "Reset pin");
CORIUM_PSTR(DS_H_RST, "GPIO toggled at boot to reset the OLED, 255 = off");
#endif

// Keys from 0.12, never rename them
static const corium::Pref DISPLAY_PREF_ITEMS[] PROGMEM = {
#if DISP_MANY
  {EG_K_TYPE,       DS_L_TYP, nullptr, STR(OLED_TYPE), DS_O_TYP, 0, DS_TYPE_MAX, 0, corium::P_ENUM, corium::P_REBOOT},
#endif
  {EG_K_BRIGHTNESS, EG_L_BRT, nullptr,  "25", nullptr, 0, 100, 0, corium::P_UINT, corium::P_SLIDER},
#ifndef OLED_PINS_BLOCKED
  {K_RST,           DS_L_RST, DS_H_RST, STR(OLED_RST), nullptr, 0, 255, 0, corium::P_UINT, corium::P_ADVANCED | corium::P_REBOOT},
#endif
  {K_FLIP,          DS_L_FLP, nullptr, OLED_FLIP ? "1" : "0", nullptr, 0, 1, 0, corium::P_BOOL, corium::P_ADVANCED | corium::P_REBOOT},
#ifdef GEIGER_PUSHBUTTON
  {K_TIMEOUT,       DS_L_TMO, DS_H_TMO, "120", nullptr, 0, 99999, 0, corium::P_UINT, 0},
#endif
  {K_ON_TIME,       DS_L_ONT, DS_H_ONT, "", nullptr, 0, 0, CORIUM_HHMM_MAX, corium::P_STRING, corium::P_TIME | corium::P_ADVANCED},
  {K_OFF_TIME,      DS_L_OFT, DS_H_OFT, "", nullptr, 0, 0, CORIUM_HHMM_MAX, corium::P_STRING, corium::P_TIME | corium::P_ADVANCED},
};

static const corium::PrefGroup DISPLAY_PREF_GROUP PROGMEM = {
  "display", "Display", 1,
  DISPLAY_PREF_ITEMS,
  sizeof(DISPLAY_PREF_ITEMS) / sizeof(DISPLAY_PREF_ITEMS[0]),
};

const corium::PrefGroup* Display::prefsGroup() { return &DISPLAY_PREF_GROUP; }

#if defined(MAX7219_DISPLAY) && DISP_MANY
static const uint16_t SHOW_OLED = DISPLAY_TYPES & ~DISP_LED_TYPES;
static const corium::PrefShow DISPLAY_SHOW[] PROGMEM = {
#ifndef OLED_PINS_BLOCKED
  {K_RST,  EG_K_DISP_TYPE, SHOW_OLED},
#endif
  {K_FLIP, EG_K_DISP_TYPE, SHOW_OLED},
  {},
};
const corium::PrefShow* Display::prefsShow() { return DISPLAY_SHOW; }
#else
const corium::PrefShow* Display::prefsShow() { return nullptr; }
#endif

static const corium::LegacyAlias DISPLAY_LEGACY[] PROGMEM = {
  {"dispBrightness", "brightness"},
#ifdef GEIGER_PUSHBUTTON
  {"dispTimeout", "timeout"},
#endif
  {"oledOn",  "on_time"},
  {"oledOff", "off_time"},
  {},
};
corium::LegacyRows Display::legacyAliases() { return DISPLAY_LEGACY; }

static DisplayBackend* s_backend = nullptr;   // set once, on the first prefs load

// Backends add themselves before setup
static DisplayBackend* s_all[Display::BACKENDS_MAX];
static uint8_t s_count = 0;

bool Display::add(DisplayBackend* b) {
  if (s_count >= BACKENDS_MAX) return false;
  s_all[s_count++] = b;
  return true;
}

static DisplayBackend* chosen() {
  uint8_t type = OLED_TYPE;
#if defined(MAX7219_DISPLAY) && DISP_MANY
  type = (uint8_t)corium::Prefs::getUInt("display", EG_K_TYPE);
#endif
  for (uint8_t i = 0; i < s_count; i++) {
    if (s_all[i]->takes(type)) return s_all[i];
  }
  return s_all[0];
}

DisplayBackend* Display::backend() { return s_backend ? s_backend : chosen(); }

// Off hours run from the off time to the on time. on == off, or a blank, never sleeps
static corium::QuietWindow s_off_hours;

static void read_timing(uint32_t& timeout_ms) {
#ifdef GEIGER_PUSHBUTTON
  timeout_ms = corium::Prefs::getUInt("display", K_TIMEOUT) * 1000;
#else
  timeout_ms = 0;
#endif
  s_off_hours.set(corium::Prefs::getString("display", K_OFF_TIME),
                  corium::Prefs::getString("display", K_ON_TIME));
}

void Display::onPrefsLoaded() {
  read_timing(_timeout_ms);
  if (_started) return;
  _started = true;
  _woke.mark(0);   // boot
  find_rows();
  corium::Registry::on(Counter::TOPIC_COUNTS, this);
#ifdef GEIGER_PUSHBUTTON
  corium::Registry::on(Topic::PRESS, this);
  corium::Registry::on(Topic::BACK, this);
  corium::Registry::on(Topic::LONG, this);
#endif
  s_backend = chosen();
  for (uint8_t i = 0; i < s_count; i++) {
    if (s_all[i] != s_backend) s_all[i]->unchosen();
  }
  s_backend->start();
}

void Display::onPrefsSaved() {
  read_timing(_timeout_ms);
  s_backend->reload();
}

#ifdef APP_NOTICE
static const char* s_notice = nullptr;

const char* Notice::shown() { return s_notice; }

// dark in the off hours stays dark, the next wake shows it
void Notice::show(const char* msg) {
  s_notice = msg;
  unsigned long now = corium::fastMillis();
  if (display.sleep(now) != Display::OFF_HOURS) display.wake(now);
}

void Notice::end(const char* msg) {
  if (s_notice == msg) s_notice = nullptr;
}
#endif

// The first tap only wakes it
void Display::button(unsigned long now, int8_t dir) {
  _woke.mark(now);
  if (!_lit) {
    refresh(now);
    return;
  }
#ifdef APP_NOTICE
  // a tap clears the notice and does nothing else
  if (s_notice) {
    s_notice = nullptr;
    return;
  }
#endif
  if (dir > 0) {
    _taps = (now - _tap_ms < 500) ? _taps + 1 : 1;
    _tap_ms = now;
    if (_taps >= 5) {
      _taps = 0;
      backend()->fx(now);
      return;
    }
  }
  backend()->button(now, dir);
}

#ifdef GEIGER_PUSHBUTTON
bool Display::toggle_auto_off(unsigned long now) {
  _auto_off = !_auto_off;
  _woke.mark(now);
  refresh(now);
  backend()->screen_mode(now, _auto_off);
  return _auto_off;
}

// A short press pages on or back, a long one flips the screen mode
void Display::presses(unsigned long now) {
  uint16_t ev = corium::Registry::events(this);
  if (ev & (1u << Topic::PRESS)) button(now, (ev & (1u << Topic::BACK)) ? -1 : 1);
  if (ev & (1u << Topic::LONG)) corium::LedSignal::play(toggle_auto_off(now) ? LED_DISP_ON : LED_DISP_OFF);
}
#endif

Display::Sleep Display::sleep(unsigned long now) {
  if (!_auto_off) return AWAKE;
  if (alert()) _woke.mark(now);   // an alert wakes it
  // times out once idle for longer than the timeout, hence the + 1
  if (_timeout_ms > 0) return _woke.fresh(now, _timeout_ms + 1) ? AWAKE : TIMED_OUT;
  if (!_woke.fresh(now, 30000 + 1) && !on_hours()) return OFF_HOURS;
  return AWAKE;
}

bool Display::on_hours() { return !s_off_hours.active(); }

static bool screenOr503(EGHttpResponse& res, const uint8_t*& buf, uint16_t& w, uint16_t& h,
                        bool bitmap) {
  if (display.backend()->screen(buf, w, h) && w && !(h & 7) && (h || !bitmap)) return true;
  res.send(503, FPSTR(HTTP_CT_PLAIN), F("no display"));
  return false;
}

static constexpr uint8_t BMP_HEAD = 62;

// 1 bit BMP with a 2 colour table, rows bottom up and padded to 4 bytes.
// The screen buffer is in 8 row pages, lsb on top
static void hScreenBmp(EGHttpRequest&, EGHttpResponse& res, void*) {
  const uint8_t* buf = nullptr;
  uint16_t w = 0, h = 0;
  if (!screenOr503(res, buf, w, h, true)) return;
  const uint16_t row_unpadded = (w + 7) / 8;
  const uint16_t row_padded   = (row_unpadded + 3) & ~3;
  uint8_t row[24] = {};
  if (row_padded > sizeof(row)) { res.send(503, FPSTR(HTTP_CT_PLAIN), F("display too wide")); return; }
  const uint32_t pixel_bytes  = (uint32_t)row_padded * h;
  const uint32_t file_size    = BMP_HEAD + pixel_bytes;

  uint8_t hdr[BMP_HEAD] = {};
  hdr[0] = 'B'; hdr[1] = 'M';
  wrLe32(hdr + 2, file_size);
  hdr[10] = BMP_HEAD;
  hdr[14] = 40;
  wrLe16(hdr + 18, w);
  wrLe16(hdr + 22, h);
  hdr[26] = 1;
  hdr[28] = 1;
  hdr[46] = 2;
  hdr[58] = 255; hdr[59] = 255; hdr[60] = 255;

  if (!res.beginChunked(200, F("image/bmp"))) return;
  res.sendChunk((const char*)hdr, sizeof(hdr));

  for (int y_bmp = 0; y_bmp < h; y_bmp++) {
    int y_disp = h - 1 - y_bmp;
    int page = y_disp >> 3;
    int bit_in_page = y_disp & 7;
    for (uint16_t x_byte = 0; x_byte < row_padded; x_byte++) row[x_byte] = 0;
    for (uint16_t x_byte = 0; x_byte < row_unpadded; x_byte++) {
      uint8_t b = 0;
      const uint8_t* col_base = buf + page * w + x_byte * 8;
      for (int i = 0; i < 8; i++) {
        if ((x_byte * 8 + i) < w && ((col_base[i] >> bit_in_page) & 1)) {
          b |= (1 << (7 - i));
        }
      }
      row[x_byte] = b;
    }
    res.sendChunk((const char*)row, row_padded);
  }
  res.endChunked();
}

// w and h as le16, then the pages. h 0 means w bytes of segments
static void hScreenBin(EGHttpRequest&, EGHttpResponse& res, void*) {
  const uint8_t* buf = nullptr;
  uint16_t w = 0, h = 0;
  if (!screenOr503(res, buf, w, h, false)) return;
  if (!res.beginChunked(200, F("application/octet-stream"))) return;
  uint8_t hdr[4] = { (uint8_t)(w & 0xff), (uint8_t)(w >> 8),
                     (uint8_t)(h & 0xff), (uint8_t)(h >> 8) };
  res.sendChunk((const char*)hdr, 4);
  res.sendChunk((const char*)buf, h ? (size_t)w * (h / 8) : w);
  res.endChunked();
}

static void hScreenTap(EGHttpRequest&, EGHttpResponse& res, void*) {
  display.button(corium::fastMillis(), 1);
  gcounter.reset_alarm();   // as the button does
  res.send(200, FPSTR(HTTP_CT_PLAIN), F("ok"));
}

static const char SCREEN_PAGE_BODY[] PROGMEM = R"HTML(
<style>
.oledwrap{display:flex;flex-direction:column;align-items:center;gap:1em;margin:1em 0}
.oledframe{padding:8px;background:#222;border-radius:8px;box-shadow:0 0 10px #0006;max-width:100%}
canvas{image-rendering:pixelated;display:block;background:#000;width:512px;max-width:100%;height:auto}
.ctrls{display:flex;gap:.6em;align-items:center;flex-wrap:wrap;justify-content:center}
button.tap{padding:.7em 1.5em;font-size:1em;cursor:pointer}
.ms{display:flex;align-items:center;gap:.3em;font-size:.9em}
.ms input[type=number]{width:6em;padding:.3em}
.ms input[type=color]{width:2.3em;height:2em;padding:0;border:1px solid #888;cursor:pointer}
.ms input[type=text]{width:5.5em;padding:.3em;font-family:monospace}
.fps{font-size:.85em;opacity:.65;font-family:monospace;min-width:5em;text-align:right}
</style>
<div class=oledwrap>
<div class=oledframe><canvas id=oled width=128 height=64></canvas></div>
<div class=ctrls>
<button class=tap id=tap type=button>Tap (cycle page)</button>
<label class=ms>Refresh <input id=ivl type=number min=50 max=10000 step=50 value=500>ms</label>
<label class=ms>Tint <input id=col type=color value=#ffffff><input id=colt aria-label=Tint type=text maxlength=7 value=#ffffff></label>
<span class=fps id=fps>--</span>
</div>
</div>
<script src=/screen.js)HTML" CORIUM_CACHE_BUST R"HTML(></script>
)HTML";

extern const char screenJS[] PROGMEM = R"JS(
const $=byID,D=document,LS=localStorage,DN=Date.now,
  C=$('oled'),x=C.getContext('2d'),
  F=$('fps'),V=$('ivl'),M=$('col'),H=$('colt'),
  N=()=>{let v=+V.value;return v<50?50:v>10000?10000:v},
  OFF=0xFF000000>>>0,
  hx=(s,i)=>parseInt(s.slice(i,i+2),16),
  RE=/^#[0-9a-fA-F]{6}$/;
let I=x.createImageData(128,64),P=new Uint32Array(I.data.buffer);
let T=DN(),f=0,t=0,w=128,h=64,s=1;
const TK='egoled_tint',sv=LS.getItem(TK);
if(sv&&RE.test(sv)){M.value=sv;H.value=sv}
const sT=()=>LS.setItem(TK,M.value);
const S=[[0,0,1,0],[1,0,1,1],[1,1,1,2],[0,2,1,2],[0,1,0,2],[0,0,0,1],[0,1,1,1]],DK='#222';
function rd(b){
  C.width=C.height=240;C.style.width='240px';
  x.fillStyle='#000';x.fillRect(0,0,240,240);
  for(let i=0;i<24;i++){const a=(225-i*270/23)*Math.PI/180;
    x.fillStyle=i<b[8]?(i<6?'#0c0':i<15?'#f90':'#f00'):DK;
    x.beginPath();x.arc(120+100*Math.cos(a),120-100*Math.sin(a),7,0,7);x.fill()}
  x.lineWidth=5;x.lineCap='round';
  for(let p=0;p<4;p++){const o=b[4+p],X=56+p*36,Y=95;
    for(let s=0;s<7;s++){const q=S[s];x.strokeStyle=o>>s&1?M.value:DK;
      x.beginPath();x.moveTo(X+q[0]*20,Y+q[1]*25);x.lineTo(X+q[2]*20,Y+q[3]*25);x.stroke()}
    x.fillStyle=o&128?M.value:DK;x.beginPath();x.arc(X+28,Y+50,3,0,7);x.fill()}
}
M.oninput=()=>{H.value=M.value;sT()};
H.oninput=()=>{let v=H.value.trim();if(v[0]!=='#')v='#'+v;if(RE.test(v)){M.value=v.toLowerCase();sT()}};
async function r(){
  try{
    const R=await fetch('/screen.bin',{cache:'no-store'});
    if(R.ok){
      const b=new Uint8Array(await R.arrayBuffer());
      if(b.length>=4){
        const nw=b[0]|(b[1]<<8),nh=b[2]|(b[3]<<8),pg=nh>>3;
        if(!nh){if(b.length>=9){rd(b);f++}}
        else if(b.length>=4+nw*pg){
          if(nw!==w||nh!==h||I.width!==w*s){
            w=nw;h=nh;s=h==8?3:1;C.width=w*s;C.height=h*s;
            const z=Math.min(512/w,256/h)|0;
            C.style.width=(w*z)+'px';
            I=x.createImageData(w*s,h*s);P=new Uint32Array(I.data.buffer);
          }
          const v=M.value,
            ON=((0xFF<<24)|(hx(v,5)<<16)|(hx(v,3)<<8)|hx(v,1))>>>0,
            DIM=s>1?0xFF282828>>>0:OFF,d=s>1?s-1:1,W=w*s;
          for(let p=0;p<pg;p++)for(let c=0;c<w;c++){
            const B=b[4+p*w+c];
            for(let i=0;i<8;i++){
              const k=B>>i&1?ON:DIM,o=(p*8+i)*s*W+c*s;
              for(let u=0;u<d;u++)for(let e=0;e<d;e++)P[o+u*W+e]=k;
            }
          }
          x.putImageData(I,0,0);f++;
        }
      }
    }
  }catch(e){}
  const n=DN();
  if(n-T>=1000){F.textContent=(f*1000/(n-T)).toFixed(1)+' fps';T=n;f=0}
  t=setTimeout(r,D.hidden?1000:N());
}
$('tap').onclick=()=>fetch('/screen/tap',{cache:'no-store'});
V.onchange=()=>{clearTimeout(t);r()};
D.onvisibilitychange=()=>{if(!D.hidden){clearTimeout(t);r()}};
r();
)JS";

static void hScreen(EGHttpRequest&, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, FPSTR(EG_W_SCREEN));
  res.sendChunk(FPSTR(SCREEN_PAGE_BODY));
  corium::WebPortal::endPage(res);
}

static void hScreenJs(EGHttpRequest&, EGHttpResponse& res, void*) {
#if EG_GZ_screenJS
  corium::WebPortal::sendGz(res, FPSTR(HTTP_CT_JS), screenJS_GZ, screenJS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(screenJS));
#endif
}

void Display::registerRoutes(EGHttpServer& http) {
  http.on(F("/screen"),     EGHttpRequest::GET, hScreen);
  http.on(F("/screen.js"),  EGHttpRequest::GET, hScreenJs);
  http.on(F("/screen.bmp"), EGHttpRequest::GET, hScreenBmp);
  http.on(F("/screen.bin"), EGHttpRequest::GET, hScreenBin);
  http.on(F("/screen/tap"), EGHttpRequest::GET, hScreenTap);
}
#endif
