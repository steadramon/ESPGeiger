/*
  Display.cpp - see header.

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
#ifdef SSD1306_DISPLAY
#include "DisplayType.h"
#include "screenJS.gz.h"
#include "OLEDDisplay/OLEDDisplay.h"
#include "Corium/Module/EGModuleRegistry.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Output/QuietWindow.h"
#include "Corium/WebPortal/WebPortal.h"
#include <EGHttpServer.h>
#ifdef MAX7219_DISPLAY
#include "Max7219.h"
#endif

Display display;
EG_REGISTER_MODULE(display)

#define DISP_STR_(x) #x
#define DISP_STR(x) DISP_STR_(x)

EG_PSTR(K_RST,      "rst");
EG_PSTR(K_FLIP,     "flip");
EG_PSTR(K_TIMEOUT,  "timeout");
EG_PSTR(K_ON_TIME,  "on_time");
EG_PSTR(K_OFF_TIME, "off_time");

#ifdef GEIGER_PUSHBUTTON
EG_PSTR(DS_L_TMO, "Timeout");
EG_PSTR(DS_H_TMO, "Idle seconds before display blanks (0 = use schedule below)");
#endif
EG_PSTR(DS_L_ONT, "On Time");
EG_PSTR(DS_L_OFT, "Off Time");
#ifdef GEIGER_PUSHBUTTON
EG_PSTR(DS_H_ONT, "Display on (used when Timeout = 0)");
EG_PSTR(DS_H_OFT, "Display off (used when Timeout = 0)");
#else
EG_PSTR(DS_H_ONT, "Display on");
EG_PSTR(DS_H_OFT, "Display off");
#endif
#ifndef OLED_PINS_BLOCKED
EG_PSTR(DS_L_FLP, "Flip 180\xC2\xB0");
EG_PSTR(DS_L_TYP, "Display Type");
#ifdef ESP32
#ifdef MAX7219_DISPLAY
EG_PSTR(DS_O_TYP, "SSD1306|SH1106|SSD1309|Tiny SSD1306|Tiny SH1106|7-Seg (MAX7219)|Round (Mottram MAX7219)|Matrix (MAX7219)|7-Seg (TM1637)");
#else
EG_PSTR(DS_O_TYP, "SSD1306|SH1106|SSD1309|Tiny SSD1306|Tiny SH1106");
#endif
#else
#ifdef MAX7219_DISPLAY
// blanks keep the esp32 numbers, tiny is esp32 only
EG_PSTR(DS_O_TYP, "SSD1306|SH1106 (1.3in)|SSD1309|||7-Seg (MAX7219)|Round (Mottram MAX7219)|Matrix (MAX7219)|7-Seg (TM1637)");
#else
EG_PSTR(DS_O_TYP, "SSD1306|SH1106 (1.3in)|SSD1309");
#endif
#endif
static_assert(sizeof(DS_O_TYP) - 1 <= EGPREF_PATTERN_MAX, "display types past the options buffer");
EG_PSTR(DS_L_RST, "Reset Pin");
EG_PSTR(DS_H_RST, "GPIO toggled at boot to reset OLED (255 = none). Reboot to apply.");
#endif

#ifdef MAX7219_DISPLAY
#define DS_TYPE_MAX DISP_TM1637
#else
#define DS_TYPE_MAX (DISP_OLED_COUNT - 1)
#endif

// 0.12 keys, never rename
static const EGPref DISPLAY_PREF_ITEMS[] PROGMEM = {
#ifndef OLED_PINS_BLOCKED
  {EG_K_TYPE,       DS_L_TYP, EG_H_RBA, DISP_STR(OLED_TYPE), DS_O_TYP, 0, DS_TYPE_MAX, 0, EGP_ENUM, EGP_REBOOT},
#endif
  {EG_K_BRIGHTNESS, EG_L_BRT, nullptr,  "25", nullptr, 0, 100, 0, EGP_UINT, EGP_SLIDER},
#ifndef OLED_PINS_BLOCKED
  {K_RST,           DS_L_RST, DS_H_RST, DISP_STR(OLED_RST), nullptr, 0, 255, 0, EGP_UINT, EGP_ADVANCED | EGP_REBOOT},
  {K_FLIP,          DS_L_FLP, EG_H_RBA, OLED_FLIP ? "1" : "0", nullptr, 0, 1, 0, EGP_BOOL, EGP_ADVANCED | EGP_REBOOT},
#endif
#ifdef GEIGER_PUSHBUTTON
  {K_TIMEOUT,       DS_L_TMO, DS_H_TMO, "120", nullptr, 0, 99999, 0, EGP_UINT, 0},
#endif
  {K_ON_TIME,       DS_L_ONT, DS_H_ONT, "", nullptr, 0, 0, EG_HHMM_MAX, EGP_STRING, EGP_TIME | EGP_ADVANCED},
  {K_OFF_TIME,      DS_L_OFT, DS_H_OFT, "", nullptr, 0, 0, EG_HHMM_MAX, EGP_STRING, EGP_TIME | EGP_ADVANCED},
};

static const EGPrefGroup DISPLAY_PREF_GROUP PROGMEM = {
  "display", "Display", 1,
  DISPLAY_PREF_ITEMS,
  sizeof(DISPLAY_PREF_ITEMS) / sizeof(DISPLAY_PREF_ITEMS[0]),
};

const EGPrefGroup* Display::prefs_group() { return &DISPLAY_PREF_GROUP; }

#if defined(MAX7219_DISPLAY) && !defined(OLED_PINS_BLOCKED)
EG_PSTR(DS_W_TYPE, "display.type");
static const uint16_t SHOW_OLED = (1u << DISP_OLED_COUNT) - 1;   // OLED types come first
static const EGPrefShow DISPLAY_SHOW[] PROGMEM = {
  {K_RST,  DS_W_TYPE, SHOW_OLED},
  {K_FLIP, DS_W_TYPE, SHOW_OLED},
  {},
};
const EGPrefShow* Display::prefs_show() { return DISPLAY_SHOW; }
#else
const EGPrefShow* Display::prefs_show() { return nullptr; }
#endif

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias DISPLAY_LEGACY[] PROGMEM = {
  {"dispBrightness", "brightness"},
#ifdef GEIGER_PUSHBUTTON
  {"dispTimeout", "timeout"},
#endif
  {"oledOn",  "on_time"},
  {"oledOff", "off_time"},
  {},
};
EGLegacyRows Display::legacy_aliases() { return DISPLAY_LEGACY; }
// === END LEGACY IMPORT ===

static DisplayBackend* s_backend = nullptr;   // set once, first prefs load

static DisplayBackend* chosen() {
#ifdef MAX7219_DISPLAY
  if (DISP_IS_MAX7219((uint8_t)EGPrefs::getUInt("display", EG_K_TYPE))) return &max7219;
#endif
  return &oled;
}

DisplayBackend* Display::backend() { return s_backend ? s_backend : chosen(); }

// off to on, on == off or blank never sleeps
static QuietWindow s_off_hours;

static void read_timing(uint32_t& timeout_ms) {
#ifdef GEIGER_PUSHBUTTON
  timeout_ms = EGPrefs::getUInt("display", K_TIMEOUT) * 1000;
#else
  timeout_ms = 0;
#endif
  s_off_hours.set(EGPrefs::getString("display", K_OFF_TIME),
                  EGPrefs::getString("display", K_ON_TIME));
}

void Display::on_prefs_loaded() {
  read_timing(_timeout_ms);
  if (_started) return;
  _started = true;
  s_backend = chosen();
#ifdef MAX7219_DISPLAY
  if (s_backend != &oled) oled.load_pins();   // env sensor needs i2c
#endif
  s_backend->start();
}

void Display::on_prefs_saved() {
  read_timing(_timeout_ms);
  s_backend->reload();
}

void Display::button(unsigned long now, int8_t dir) {
  _woken = now;
  backend()->button(now, dir);
}

#ifdef GEIGER_PUSHBUTTON
bool Display::toggle_auto_off(unsigned long now) {
  _auto_off = !_auto_off;
  _woken = now;
  backend()->screen_mode(now, _auto_off);
  return _auto_off;
}
#endif

Display::Sleep Display::sleep(unsigned long now) {
  if (!_auto_off) return AWAKE;
  unsigned long idle = now - _woken;
  if (_timeout_ms > 0) return (idle > _timeout_ms) ? TIMED_OUT : AWAKE;
  if (idle > 30000 && !on_hours()) return OFF_HOURS;
  return AWAKE;
}

bool Display::on_hours() { return !s_off_hours.active(); }

static bool screenOr503(EGHttpResponse& res, const uint8_t*& buf, uint16_t& w, uint16_t& h,
                        bool bitmap) {
  if (display.backend()->screen(buf, w, h) && w && !(h & 7) && (h || !bitmap)) return true;
  res.send(503, FPSTR(HTTP_CT_PLAIN), F("no display"));
  return false;
}

static void hScreenBmp(EGHttpRequest&, EGHttpResponse& res, void*) {
  const uint8_t* buf = nullptr;
  uint16_t W = 0, H = 0;
  if (!screenOr503(res, buf, W, H, true)) return;
  const uint16_t row_unpadded = (W + 7) / 8;
  const uint16_t row_padded   = (row_unpadded + 3) & ~3;
  uint8_t row[24] = {0};
  if (row_padded > sizeof(row)) { res.send(503, FPSTR(HTTP_CT_PLAIN), F("display too wide")); return; }
  const uint32_t pixel_bytes  = (uint32_t)row_padded * H;
  const uint32_t file_size    = 62 + pixel_bytes;

  uint8_t hdr[62] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = file_size & 0xff; hdr[3] = (file_size >> 8) & 0xff;
  hdr[4] = (file_size >> 16) & 0xff; hdr[5] = (file_size >> 24) & 0xff;
  hdr[10] = 62;
  hdr[14] = 40;
  hdr[18] = W & 0xff;  hdr[19] = (W >> 8) & 0xff;
  hdr[22] = H & 0xff;  hdr[23] = (H >> 8) & 0xff;
  hdr[26] = 1;
  hdr[28] = 1;
  hdr[46] = 2;
  hdr[58] = 255; hdr[59] = 255; hdr[60] = 255;

  if (!res.beginChunked(200, F("image/bmp"))) return;
  res.sendChunk((const char*)hdr, sizeof(hdr));

  for (int y_bmp = 0; y_bmp < H; y_bmp++) {
    int y_disp = H - 1 - y_bmp;
    int page = y_disp >> 3;
    int bit_in_page = y_disp & 7;
    for (uint16_t x_byte = 0; x_byte < row_padded; x_byte++) row[x_byte] = 0;
    for (uint16_t x_byte = 0; x_byte < row_unpadded; x_byte++) {
      uint8_t b = 0;
      const uint8_t* col_base = buf + page * W + x_byte * 8;
      for (int i = 0; i < 8; i++) {
        if ((x_byte * 8 + i) < W && ((col_base[i] >> bit_in_page) & 1)) {
          b |= (1 << (7 - i));
        }
      }
      row[x_byte] = b;
    }
    res.sendChunk((const char*)row, row_padded);
  }
  res.endChunked();
}

static void hScreenBin(EGHttpRequest&, EGHttpResponse& res, void*) {
  const uint8_t* buf = nullptr;
  uint16_t W = 0, H = 0;
  if (!screenOr503(res, buf, W, H, false)) return;
  if (!res.beginChunked(200, F("application/octet-stream"))) return;
  uint8_t hdr[4] = { (uint8_t)(W & 0xff), (uint8_t)(W >> 8),
                     (uint8_t)(H & 0xff), (uint8_t)(H >> 8) };
  res.sendChunk((const char*)hdr, 4);
  res.sendChunk((const char*)buf, H ? (size_t)W * (H / 8) : W);
  res.endChunked();
}

static void hScreenTap(EGHttpRequest&, EGHttpResponse& res, void*) {
  display.button(fast_millis(), 1);
  res.send(200, FPSTR(HTTP_CT_PLAIN), F("ok"));
}

static const char SCREEN_PAGE_BODY[] PROGMEM = R"HTML(
<style>
  .oledwrap{display:flex;flex-direction:column;align-items:center;gap:1em;margin:1em 0}
  .oledframe{padding:8px;background:#222;border-radius:8px;box-shadow:0 0 10px #0006}
  canvas{image-rendering:pixelated;display:block;background:#000;width:512px;height:256px}
  .ctrls{display:flex;gap:.6em;align-items:center;flex-wrap:wrap;justify-content:center}
  button.tap{padding:.7em 1.5em;font-size:1em;cursor:pointer}
  .ms{display:flex;align-items:center;gap:.3em;font-size:.9em}
  .ms input[type=number]{width:6em;padding:.3em}
  .ms input[type=color]{width:2.3em;height:2em;padding:0;border:1px solid #888;cursor:pointer}
  .ms input[type=text]{width:5.5em;padding:.3em;font-family:monospace}
  .fps{font-size:.85em;opacity:.65;font-family:monospace;min-width:5em;text-align:right}
</style>
<div class="oledwrap">
  <div class="oledframe"><canvas id="oled" width="128" height="64"></canvas></div>
  <div class="ctrls">
    <button class="tap" id="tap" type="button">Tap (cycle page)</button>
    <label class="ms">Refresh <input id="ivl" type="number" min="50" max="10000" step="50" value="500">ms</label>
    <label class="ms">Tint <input id="col" type="color" value="#ffffff"><input id="colt" type="text" maxlength="7" value="#ffffff"></label>
    <span class="fps" id="fps">--</span>
  </div>
</div>
<script src=/screen.js)HTML" EG_CACHE_BUST R"HTML(></script>
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
  C.width=C.height=240;C.style.width=C.style.height='240px';
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
            C.style.width=(w*z)+'px';C.style.height=(h*z)+'px';
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
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Screen"));
  res.sendChunk(FPSTR(SCREEN_PAGE_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hScreenJs(EGHttpRequest&, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if EG_GZ_screenJS
  res.sendGzipP(200, FPSTR(HTTP_CT_JS), screenJS_GZ, screenJS_GZ_LEN);
#else
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
