/*
  GRNG.cpp - RNG wrapper

  Copyright (C) 2023 @steadramon

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
#include "GRNG.h"
#include "../Logger/Logger.h"
#include "../Module/EGModule.h"
#include "../WebPortal/WebPortal.h"
#include "sha256.h"
#include <EGHttpServer.h>
#include <string.h>
#include <stdlib.h>

static EG_XTASK_VOLATILE uint32_t s_pool[8] = {0};
static uint8_t s_mix_idx = 0;

static inline uint32_t hw_word() {
#ifdef ESP8266
  return RANDOM_REG32;
#else
  return esp_random();
#endif
}

#ifndef ESP8266
static uint8_t s_boot_sram[256] __attribute__((section(".noinit")));
#endif

GRNG::GRNG() {
};

void GRNG::begin() {
  Sha256.init();
#ifdef ESP8266
  Sha256.write((const uint8_t*)(0x3FFFC000 - 0x800), 0x800);
#else
  Sha256.write(s_boot_sram, sizeof(s_boot_sram));
#endif
  uint32_t cc = ESP.getCycleCount();
  Sha256.write((const uint8_t*)&cc, sizeof(cc));
  uint8_t* h = Sha256.result();
  for (uint8_t i = 0; i < 8; i++) s_pool[i] ^= ((uint32_t*)h)[i];
#ifndef ESP8266
  memcpy(s_boot_sram, h, 32);
#endif
  for (uint8_t i = 0; i < 8; i++) mix(hw_word());
  stir();
}

void GRNG::mix(uint32_t bits) {
  s_pool[s_mix_idx++ & 7] ^= bits;
}

uint32_t GRNG::stir() {
  uint32_t e = hw_word() ^ ESP.getCycleCount();
  mix(e);
  randomSeed(e ^ s_pool[0]);
  return e;
}

void GRNG::extract(uint8_t* out, size_t n) {
  while (n > 0) {
    Sha256.init();
    Sha256.write((const uint8_t*)s_pool, sizeof(s_pool));
    uint32_t cc = ESP.getCycleCount();
    Sha256.write((const uint8_t*)&cc, sizeof(cc));
    uint32_t hw[4];
    for (uint8_t i = 0; i < 4; i++) hw[i] = hw_word();
    Sha256.write((const uint8_t*)hw, sizeof(hw));
    uint8_t* h = Sha256.result();
    size_t take = n > 32 ? 32 : n;
    memcpy(out, h, take);
    out += take;
    n -= take;
    for (uint8_t i = 0; i < 8; i++) s_pool[i] ^= ((uint32_t*)h)[i];
  }
}

uint32_t GRNG::fast_uint32() {
  static uint8_t buf[32];
  static uint8_t pos = sizeof(buf);
  if (pos >= sizeof(buf)) {
    extract(buf, sizeof(buf));
    pos = 0;
  }
  uint32_t r;
  memcpy(&r, buf + pos, sizeof(r));
  pos += sizeof(r);
  return r;
}

void GRNG::extract_fast(uint8_t* out, size_t n) {
  static uint32_t s_xs = 0;
  uint32_t x = s_xs ^ fast_uint32();
  if (x == 0) x = 0x6b8b4567;
  while (n > 0) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    size_t take = n > 4 ? 4 : n;
    memcpy(out, &x, take);
    out += take;
    n -= take;
  }
  s_xs = x;
}

// ---------- /random and /random.do handlers ----------

static const char RANDOM_PAGE_BODY_NEW[] PROGMEM = R"HTML(
<style>
.rg th{text-align:right;color:var(--muted);font-weight:normal;padding:.3em .7em .3em 0}
.rg td{padding:.3em .4em}
.rg input[type=number]{width:4em;padding:.3em .4em;text-align:center}
.rg button{min-width:6em;padding:.4em .8em;width:auto}
.rg .o{display:inline-block;width:18em;padding:.3em .6em;background:var(--card);border:1px solid var(--border);border-radius:4px;font-family:monospace;font-size:.92em;white-space:nowrap;overflow-x:auto;vertical-align:middle;box-sizing:border-box}
.rg .o.f{animation:rgf .55s ease-out}
@keyframes rgf{0%{background:var(--accent);color:#fff}100%{background:var(--card);color:inherit}}
</style>
<table class=rg id=rg></table>
<p class=muted>Powered by accumulated radiation events.</p>
<script>
[['Coin','coin',null,'Flip'],
 ['Dice D','dice',['sides',6,2,10000],'Roll'],
 ['Password','password',['len',16,4,64],'Generate','chars'],
 ['Hex','hex',['n',16,1,32],'Generate','bytes'],
 ['Bias test','flip',['n',1000,10,10000],'Run','flips'],
 ['OTP','otp',['n',256,16,1024],'Download','bytes',1]
].forEach(c=>{
  var t=document.createElement('tr'),
      i=c[2]?'<input type=number value='+c[2][1]+' min='+c[2][2]+' max='+c[2][3]+'>':'',
      u=c[4]?'<span class=muted>'+c[4]+'</span>':'';
  t.innerHTML='<th>'+c[0]+'</th><td>'+i+'</td><td>'+u+'</td><td><button type=button>'+c[3]+'</button></td><td><span class=o></span></td>';
  t.dataset.type=c[1];if(c[2])t.dataset.arg=c[2][0];if(c[5])t.dataset.dl=1;
  t.querySelector('button').onclick=function(){r(this)};
  document.getElementById('rg').appendChild(t);
});
function r(b){
  var row=b.closest('tr'),t=row.dataset.type,k=row.dataset.arg,
      i=row.querySelector('input'),o=row.querySelector('.o'),dl=row.dataset.dl;
  o.classList.remove('f');b.disabled=true;
  var u='/random.do?type='+t;if(i&&k)u+='&'+k+'='+i.value;
  fetch(u).then(r=>r.text()).then(x=>{
    if(dl){
      var a=document.createElement('a');
      a.href=URL.createObjectURL(new Blob([x],{type:'text/plain'}));
      a.download=t+'-'+Date.now()+'.txt';
      a.click();URL.revokeObjectURL(a.href);
      o.textContent=i.value+' bytes ('+x.length+' hex chars) saved';
    }else o.textContent=x;
    void o.offsetWidth;o.classList.add('f');b.disabled=false;
  }).catch(()=>{o.textContent='(error)';b.disabled=false;});
}
</script>
)HTML";

static void hRandomPage(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("Random"));
  res.sendChunk(FPSTR(RANDOM_PAGE_BODY_NEW));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hRandomDo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* type = req.arg("type");
  if (!type) { res.send(400, "text/plain", ""); return; }

  // OTP: hex-encoded, chunked. N requested bytes -> 2N hex chars.
  if (strcmp(type, "otp") == 0) {
    const char* na = req.arg("n");
    long n = na ? atol(na) : 256;
    if (n < 16)   n = 16;
    if (n > 1024) n = 1024;
    res.beginChunked(200, "text/plain");
    uint8_t  chunk[64];
    char     hexbuf[128];
    long remaining = n;
    while (remaining > 0) {
      size_t batch = remaining > 64 ? 64 : (size_t)remaining;
      GRNG::extract(chunk, batch);
      for (size_t i = 0; i < batch; i++) {
        hexbuf[i*2]     = "0123456789abcdef"[chunk[i] >> 4];
        hexbuf[i*2 + 1] = "0123456789abcdef"[chunk[i] & 0xF];
      }
      res.sendChunk(hexbuf, batch * 2);
      remaining -= batch;
    }
    res.endChunked();
    return;
  }

  char out[68];
  size_t outlen = 0;

  if (strcmp(type, "coin") == 0) {
    uint8_t b;
    GRNG::extract(&b, 1);
    const char* s = (b & 1) ? "Heads" : "Tails";
    outlen = strlen(s);
    memcpy(out, s, outlen);
  } else if (strcmp(type, "dice") == 0) {
    const char* sa = req.arg("sides");
    long sides = sa ? atol(sa) : 6;
    if (sides < 2)     sides = 6;
    if (sides > 10000) sides = 10000;
    uint8_t bytes[4];
    GRNG::extract(bytes, 4);
    uint32_t r = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                 ((uint32_t)bytes[2] <<  8) |  (uint32_t)bytes[3];
    long roll = (long)(r % (uint32_t)sides) + 1;
    outlen = snprintf_P(out, sizeof(out), PSTR("%ld"), roll);
  } else if (strcmp(type, "hex") == 0) {
    // Up to 256 B in chunks; chunked response because output exceeds `out`.
    const char* na = req.arg("n");
    long n = na ? atol(na) : 16;
    if (n < 1)   n = 16;
    if (n > 256) n = 256;
    res.beginChunked(200, "text/plain");
    uint8_t chunk[32];
    char hex[65];
    long remaining = n;
    while (remaining > 0) {
      size_t batch = remaining > 32 ? 32 : (size_t)remaining;
      GRNG::extract(chunk, batch);
      for (size_t i = 0; i < batch; i++) {
        hex[i*2]     = "0123456789abcdef"[chunk[i] >> 4];
        hex[i*2 + 1] = "0123456789abcdef"[chunk[i] & 0xF];
      }
      res.sendChunk(hex, batch * 2);
      remaining -= batch;
    }
    res.endChunked();
    return;
  } else if (strcmp(type, "password") == 0) {
    const char* la = req.arg("len");
    long len = la ? atol(la) : 16;
    if (len < 4)  len = 16;
    if (len > 64) len = 64;
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    uint8_t bytes[64];
    GRNG::extract(bytes, len);
    for (long i = 0; i < len; i++) out[i] = chars[bytes[i] % (sizeof(chars) - 1)];
    outlen = len;
  } else if (strcmp(type, "flip") == 0) {
    // Many-flip bias test: count heads in N coin flips, report chi^2 vs 50/50.
    // chi^2 with 1 dof: <3.841 = fair (p>0.05), <6.635 = suspect, else biased.
    const char* na = req.arg("n");
    long n = na ? atol(na) : 1000;
    if (n < 10)    n = 10;
    if (n > 10000) n = 10000;
    uint32_t heads = 0;
    uint8_t  buf[64];
    long remaining = n;
    while (remaining > 0) {
      size_t need = (size_t)((remaining + 7) / 8);
      if (need > sizeof(buf)) need = sizeof(buf);
      GRNG::extract_fast(buf, need);
      for (size_t i = 0; i < need && remaining > 0; i++) {
        uint8_t b = buf[i];
        for (int k = 0; k < 8 && remaining > 0; k++) {
          if (b & (1 << k)) heads++;
          remaining--;
        }
      }
    }
    uint32_t tails = (uint32_t)n - heads;
    float diff = (float)heads - (float)n / 2.0f;
    float chi2 = (diff * diff * 4.0f) / (float)n;
    const char* verdict = chi2 < 3.841f ? "fair"
                         : chi2 < 6.635f ? "suspect"
                                         : "biased";
    uint32_t chi2_milli = (uint32_t)(chi2 * 1000.0f + 0.5f);
    outlen = snprintf_P(out, sizeof(out),
                        PSTR("N=%ld H=%lu T=%lu chi2=%lu.%03lu (%s)"),
                        n, (unsigned long)heads, (unsigned long)tails,
                        (unsigned long)(chi2_milli / 1000UL),
                        (unsigned long)(chi2_milli % 1000UL),
                        verdict);
  } else {
    res.send(400, "text/plain", "");
    return;
  }

  out[outlen] = '\0';
  res.send(200, "text/plain", out);
}

void GRNGRoutes::registerRoutes(EGHttpServer& http) {
  http.on("/random",    EGHttpRequest::GET, hRandomPage);
  http.on("/random.do", EGHttpRequest::GET, hRandomDo);
}
