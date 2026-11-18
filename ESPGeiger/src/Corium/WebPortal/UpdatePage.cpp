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
#include "UPDATE_PAGE_JS.gz.h"
#include "Corium/System/BootGuard.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/Words.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/Logger/Logger.h"

#ifndef ESP8266
  #include <Update.h>
#endif

// The esp8266 copies the new image over at boot, so it takes longer
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
)HTML";

// normal mode script, served gzipped at /updatejs
#if !EG_GZ_UPDATE_PAGE_JS
extern const char UPDATE_PAGE_JS[] PROGMEM = R"JS(
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
sh('Keep it powered, reloading in '+n+'s...');
var pa=null,T=setTimeout;
function pn(){
if(document.hidden){T(pn,1000);return}
if(pa)pa.abort();
var b=pa=new AbortController();
T(()=>b.abort(),1000);
fetch('/ping',{cache:'no-store',signal:b.signal}).then(r=>location.href='/').catch(()=>T(pn,500));
}
var iv=setInterval(function(){
if(--n>0)sh('Keep it powered, reloading in '+n+'s...');
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
)JS";
#endif

// Safe mode script, sent inline. The device still checks the image
static const char UPDATE_SAFE_JS[] PROGMEM = R"JS(<script>
byID('uf_btn').disabled=false;
byID('uf').onsubmit=function(e){e.preventDefault();var f=byID('uf_file').files[0];if(!f)return;var B=byID('uf_btn'),S=byID('uf_st');B.disabled=true;S.textContent='Uploading '+f.size+' bytes...';var A=byID('ua'),x=new XMLHttpRequest();x.open('POST','/update'+(A&&A.checked?'?any=1':''));x.setRequestHeader('Content-Type','application/octet-stream');x.onload=function(){S.textContent=x.responseText||'Done';if(x.status==200)setTimeout(function(){location.href='/'},3000)};x.onerror=function(){B.disabled=false;S.textContent='Upload failed'};x.send(f)};
</script>)JS";

// BUILD_ENV on its own would match other variants too, so it's wrapped
// It has to stay used or gc-sections drops it from the bin
static const char EG_ENV_TAG[] PROGMEM = "<EGENV:" BUILD_ENV ":ENV>";
// with CORIUM_OTA_SAME_ENV a bin without the tag is refused
#ifdef CORIUM_OTA_SAME_ENV
#define OTA_OWN ",own:1"
// Safe mode's way back to an older build, the normal page doesn't offer it
static const char UPDATE_SAFE_ANY[] PROGMEM = "<p><label><input type=checkbox id=ua> Another build (an older firmware)</label></p>";
#else
#define OTA_OWN ""
#endif

void corium::WebPortal::hUpdateForm(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, FPSTR(W_UPDATE));
  res.sendChunk(FPSTR(UPDATE_FORM_BODY));
  if (corium::BootGuard::safeMode()) {
#ifdef CORIUM_OTA_SAME_ENV
    res.sendChunk(FPSTR(UPDATE_SAFE_ANY));
#endif
    res.sendChunk(FPSTR(UPDATE_SAFE_JS));
  } else {
    sendf(res, PSTR("<script>window._dev={env:\"" BUILD_ENV "\",tag:\"%s\",rs:%u" OTA_OWN "};</script>"
                    "<script src=/updatejs" CORIUM_CACHE_BUST "></script>"),
      EG_ENV_TAG, (unsigned)FLASH_COUNTDOWN_SECS);
  }
  corium::WebPortal::endPage(res);
}

void corium::WebPortal::hUpdateJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_UPDATE_PAGE_JS
  sendGz(res, FPSTR(HTTP_CT_JS), UPDATE_PAGE_JS_GZ, UPDATE_PAGE_JS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(UPDATE_PAGE_JS));
#endif
}

// One upload at a time
static struct {
  size_t expected = 0;
  size_t delivered = 0;
  bool   write_error = false;
  uint8_t head[8];         // held until the entry addr is checked
  uint8_t head_n = 0;
  uint8_t tag_n = 0;       // EG_ENV_TAG chars matched so far
  bool    any = false;     // ?any=1, another env's bin is meant
  uint8_t last = 0;        // held back until the checks at the end
  uint32_t len = 0;        // 8266 only, the length the image itself gives
  bool    started = false; // OTA_START went out, so OTA_FAILED is owed on a failure
  PGM_P   why = nullptr;   // reason shown on the page
} s_ota;

static void ota_fail(PGM_P why) {
  s_ota.write_error = true;
  if (s_ota.why) return;
  s_ota.why = why;
#ifdef ESP8266
  corium::Wifi::applySleep();   // Update.begin set WiFi sleep to none, put it back
#endif
}

// Too big is SPACE on the 8266, SIZE on the esp32
static PGM_P begin_why() {
  uint8_t e = Update.getError();
  if (e == UPDATE_ERROR_SPACE || e == UPDATE_ERROR_SIZE) return PSTR("too big");
#ifdef ESP8266
  if (e == UPDATE_ERROR_OOM) return W_OOM;
#else
  if (e == UPDATE_ERROR_OK) return W_OOM;   // a failed buffer malloc sets no error
#endif
  return PSTR("begin failed");
}

static void ota_begin(size_t expected) {
  s_ota.expected    = expected;
  s_ota.delivered   = 0;
  s_ota.write_error = false;
  s_ota.head_n      = 0;
  s_ota.tag_n       = 0;
  s_ota.len         = 0;
  s_ota.started     = false;
  s_ota.why         = nullptr;
  // A restart from a save would cut the upload off
  if (corium::Lifecycle::restartPending()) {
    ota_fail(PSTR("restarting, try again"));
    return;
  }
  // with no length given use the free space less one 4 KB sector, sector aligned
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

// A bin's entry point must sit in this chip's IRAM
#ifdef ESP8266
// esp8266 IRAM
static constexpr uint32_t ENTRY_LO = 0x40100000UL, ENTRY_HI = 0x40200000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// S3 IRAM, up to below the bootloader's entry (0x403C9700) which a merged bin starts with
static constexpr uint32_t ENTRY_LO = 0x40370000UL, ENTRY_HI = 0x403C0000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
static constexpr uint32_t ENTRY_LO = 0x40020000UL, ENTRY_HI = 0x40050000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
// up to below the bootloader's entry (0x403CC710), a merged bin starts with that one
static constexpr uint32_t ENTRY_LO = 0x40380000UL, ENTRY_HI = 0x403C0000UL;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
static constexpr uint32_t ENTRY_LO = 0x40800000UL, ENTRY_HI = 0x40880000UL;
#else
// esp32 IRAM
static constexpr uint32_t ENTRY_LO = 0x40080000UL, ENTRY_HI = 0x400A0000UL;
#endif

static bool ota_entry_ok(uint32_t entry) {
  return entry >= ENTRY_LO && entry < ENTRY_HI;
}

// A merged bin is for this chip, but it's the wrong file
static PGM_P entry_why(uint32_t entry) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
  if (entry >= ENTRY_HI && entry < 0x403E0000UL) return PSTR("a merged bin, upload firmware.bin");
#else
  (void)entry;
#endif
  return PSTR("wrong platform");
}

// The 8266 end() takes gzip too, but a gzip's bytes 4-7 never pass the entry check
static constexpr uint8_t IMAGE_MAGIC = 0xE9;
static const char W_IMAGE[] PROGMEM = "image check failed";

#ifdef CORIUM_OTA_SAME_ENV
// '<' only appears at the start of the tag, so a miss goes back to 0, or 1 on a '<'
static void ota_scan(const uint8_t* p, size_t len) {
  for (size_t i = 0; i < len && s_ota.tag_n < sizeof(EG_ENV_TAG) - 1; i++) {
    if (p[i] == pgm_read_byte(&EG_ENV_TAG[s_ota.tag_n])) s_ota.tag_n++;
    else s_ota.tag_n = (p[i] == '<') ? 1 : 0;
  }
}
#endif

#ifdef ESP8266
// elf2bin's crcsize_offset holds the whole image's length, a cut file still has it
static constexpr size_t LEN_AT = 4096 + 16;

static void ota_len_scan(const uint8_t* p, size_t len) {
  size_t at = s_ota.delivered;
  if (at + len <= LEN_AT || at >= LEN_AT + 4) return;
  for (size_t i = 0; i < len; i++, at++)
    if (at >= LEN_AT && at < LEN_AT + 4) s_ota.len |= (uint32_t)p[i] << (8 * (at - LEN_AT));
}
#endif

static bool ota_write(const uint8_t* p, size_t len) {
  size_t n = len;
#ifdef CORIUM_OTA_SAME_ENV
  ota_scan(p, len);
#endif
#ifdef ESP8266
  ota_len_scan(p, len);
#endif
  // A finished update commits even on end(false), so the last byte is held back and a refused bin stays short
  if (s_ota.delivered + len == s_ota.expected) s_ota.last = p[--n];
  size_t wrote = n ? Update.write((uint8_t*)p, n) : 0;
  if (wrote != n) {
    corium::Log::console(PSTR("OTA: Short write %u/%u err=%u"),
                         (unsigned)wrote, (unsigned)n, Update.getError());
    ota_fail(PSTR("write failed"));
    return false;
  }
  s_ota.delivered += len;
  return true;
}

static void ota_data(const char* chunk, size_t len) {
  if (s_ota.write_error || !Update.isRunning()) return;   // already failed
  // Check entry_addr first, a bin for the wrong chip would brick it
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
    // refuse what end() would refuse, before a byte is written
    if (h[0] != IMAGE_MAGIC || !ota_entry_ok(entry)) {
      corium::Log::console(PSTR("OTA: REFUSING bin (magic=0x%02x entry=0x%08x)"),
                           (unsigned)h[0], (unsigned)entry);
      Update.end(false);
      ota_fail(h[0] != IMAGE_MAGIC ? W_IMAGE : entry_why(entry));
      return;
    }
    if (!ota_write(h, sizeof(s_ota.head))) return;
  }
  if (len) ota_write((const uint8_t*)chunk, len);
}

static void ota_end() {
  // Update.end() is strict and needs the full size
  if (s_ota.write_error || !Update.isRunning()) {
    corium::Log::console(PSTR("OTA: Aborting on prior error"));
    if (Update.isRunning()) Update.end(false);
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
#ifdef ESP8266
  // The image says it's longer than what came and would still fit, so the file was cut short.
  // An old core's bin has no length here. No real image ends before LEN_AT + 4 either
  if (s_ota.expected < LEN_AT + 4 || (s_ota.len > s_ota.expected && s_ota.len <= corium::DeviceInfo::freeSketch())) {
    corium::Log::console(PSTR("OTA: REFUSING, cut short (%u of %u bytes)"),
                         (unsigned)s_ota.expected, (unsigned)s_ota.len);
    Update.end(false);
    ota_fail(PSTR("file cut short"));
    return;
  }
#endif
  if (Update.write(&s_ota.last, 1) != 1) {
    Update.end(false);
    ota_fail(PSTR("write failed"));
    return;
  }
  if (Update.end()) {
    corium::Log::console(PSTR("OTA: Success (%u bytes), restarting"),
                         (unsigned)s_ota.delivered);
    // restart from here too, the reply may never go out
    corium::WebPortal::requestRestart();
  } else {
    corium::Log::console(PSTR("OTA: End failed err=%u"), Update.getError());
    ota_fail(W_IMAGE);
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
  // Runs on the main loop, so flash writes are fine here
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

// Bytes written on the device, total is 0 when idle. err is set after a refused upload
void corium::WebPortal::hUpdateProgress(EGHttpRequest& req, EGHttpResponse& res, void*) {
  char buf[80];
  snprintf_P(buf, sizeof(buf), PSTR("{\"done\":%u,\"total\":%u,\"err\":\"%s\"}"),
             (unsigned)s_ota.delivered, (unsigned)(s_ota.started ? s_ota.expected : 0),
             s_ota.why ? s_ota.why : PSTR(""));
  res.addHeader(FPSTR(HTTP_H_CACHE), F("no-store"));
  res.send(200, FPSTR(HTTP_CT_JSON), buf);
}
