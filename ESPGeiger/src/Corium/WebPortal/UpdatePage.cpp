/*
  UpdatePage.cpp - /update, the firmware upload page and its writer.

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
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/Words.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Logger/Logger.h"

#ifndef ESP8266
  #include <Update.h>   // esp32 needs it
#endif

// esp8266 copies the new image at boot
#ifdef ESP8266
#define FLASH_COUNTDOWN_SECS 20
#else
#define FLASH_COUNTDOWN_SECS 10
#endif

static const char UPDATE_FORM_BODY[] PROGMEM = R"HTML(
<p>Upload a firmware <code>.bin</code> built for this board. The device will restart when the upload completes.</p>
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
else if(D.own){B.disabled=true;sC('e','<b>Refused:</b> running <code>'+e+'</code> not found in file. This board only takes its own build.')}
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
// CORIUM_OTA_SAME_ENV: a bin without it is refused
#ifdef CORIUM_OTA_SAME_ENV
#define OTA_OWN ",own:1"
#else
#define OTA_OWN ""
#endif

void corium::WebPortal::hUpdateForm(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Update"));
  sendf(res, PSTR("<script>window._dev={env:\"" BUILD_ENV "\",tag:\"%s\",rs:%u" OTA_OWN "};</script>"),
    EG_ENV_TAG, (unsigned)FLASH_COUNTDOWN_SECS);
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
  uint8_t tag_n = 0;       // EG_ENV_TAG chars matched so far
  bool    any = false;     // ?any=1, another env's bin meant
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
  s_ota.tag_n       = 0;
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
    corium::Log::console(PSTR("OTA: Starting (%u bytes)"), (unsigned)s_ota.expected);
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

#ifdef CORIUM_OTA_SAME_ENV
// '<' only starts the tag, a miss goes back to 0 or 1
static void ota_scan(const uint8_t* p, size_t len) {
  for (size_t i = 0; i < len && s_ota.tag_n < sizeof(EG_ENV_TAG) - 1; i++) {
    if (p[i] == pgm_read_byte(&EG_ENV_TAG[s_ota.tag_n])) s_ota.tag_n++;
    else s_ota.tag_n = (p[i] == '<') ? 1 : 0;
  }
}
#endif

static bool ota_write(const uint8_t* p, size_t len) {
#ifdef CORIUM_OTA_SAME_ENV
  ota_scan(p, len);
#endif
  size_t wrote = Update.write((uint8_t*)p, len);
  if (wrote != len) {
    corium::Log::console(PSTR("OTA: Short write %u/%u err=%u"),
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
    corium::Log::console(PSTR("OTA: Aborting on prior error"));
    if (Update.isRunning()) Update.end(false);  // discard
    ota_fail(PSTR("not running"));
    return;
  }
  if (s_ota.delivered != s_ota.expected) {
    corium::Log::console(PSTR("OTA: Byte-count mismatch %u != %u"),
                 (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
    Update.end(false);
    ota_fail(PSTR("size mismatch"));
    return;
  }
#ifdef CORIUM_OTA_SAME_ENV
  if (s_ota.tag_n < sizeof(EG_ENV_TAG) - 1) {
    static const char why[] PROGMEM = "not a " BUILD_ENV " bin";
    corium::Log::console(s_ota.any ? PSTR("OTA: %s, ?any=1 takes it") : PSTR("OTA: REFUSING, %s"), why);
    if (!s_ota.any) {
      Update.end(false);
      ota_fail(why);
      return;
    }
  }
#endif
  if (Update.end()) {
    corium::Log::console(PSTR("OTA: Success (%u bytes), restarting"),
                 (unsigned)s_ota.delivered);
    // here too, reply may never go out
    corium::WebPortal::requestRestart();
  } else {
    corium::Log::console(PSTR("OTA: End failed err=%u"), Update.getError());
    ota_fail(PSTR("image check failed"));
  }
}

static void ota_abort() {
  if (Update.isRunning()) Update.end(false);
  ota_fail(PSTR("connection lost"));
  corium::Log::console(PSTR("OTA: Aborted at %u/%u bytes"),
               (unsigned)s_ota.delivered, (unsigned)s_ota.expected);
}

bool corium::WebPortal::hUpdateBody(EGHttpRequest& req, EGHttpServer::BodyEvent ev,
                             const char* chunk, size_t len, void*) {
  // main loop, flash write ok
  switch (ev) {
    case EGHttpServer::BODY_BEGIN:
#ifdef CORIUM_OTA_SAME_ENV
      s_ota.any = req.arg(F("any")) != nullptr;
#endif
      ota_begin(req.bodyLen());
      break;
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
