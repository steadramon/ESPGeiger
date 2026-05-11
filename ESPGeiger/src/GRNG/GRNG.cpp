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
#include "../WebPortal/WebPortal.h"
#include "sha256.h"
#include <EGHttpServer.h>
#include <string.h>
#include <stdlib.h>

static volatile uint32_t s_pool = 0;

GRNG::GRNG() {
};

void GRNG::begin() {
#ifdef ESP8266
  mix(RANDOM_REG32);
#else
  mix(esp_random());
#endif
  stir();
}

void GRNG::mix(uint32_t bits) {
  s_pool ^= bits;
}

uint32_t GRNG::stir() {
#ifdef ESP8266
  uint32_t hw = RANDOM_REG32;
#else
  uint32_t hw = esp_random();
#endif
  uint32_t e = hw ^ ESP.getCycleCount() ^ s_pool;
  s_pool ^= e;
  randomSeed(e);
  return e;
}

void GRNG::extract(uint8_t* out, size_t n) {
  while (n > 0) {
    Sha256.init();
    Sha256.write((const uint8_t*)&s_pool, sizeof(s_pool));
    uint32_t cc = ESP.getCycleCount();
    Sha256.write((const uint8_t*)&cc, sizeof(cc));
#ifdef ESP8266
    uint32_t hw = RANDOM_REG32;
#else
    uint32_t hw = esp_random();
#endif
    Sha256.write((const uint8_t*)&hw, sizeof(hw));
    uint8_t* h = Sha256.result();
    size_t take = n > 32 ? 32 : n;
    memcpy(out, h, take);
    out += take;
    n -= take;
    s_pool ^= ((uint32_t*)h)[0];
  }
}

uint32_t GRNG::fast_uint32() {
  static uint8_t s_call_count = 0;
  if ((++s_call_count & 0x3F) == 0) stir();
#ifdef ESP8266
  return RANDOM_REG32;
#else
  return esp_random();
#endif
}

void GRNG::extract_fast(uint8_t* out, size_t n) {
  stir();
  while (n >= 4) {
#ifdef ESP8266
    *(uint32_t*)out = RANDOM_REG32;
#else
    *(uint32_t*)out = esp_random();
#endif
    out += 4;
    n -= 4;
  }
  if (n > 0) {
#ifdef ESP8266
    uint32_t r = RANDOM_REG32;
#else
    uint32_t r = esp_random();
#endif
    memcpy(out, &r, n);
  }
}

// ---------- /random and /random.do handlers ----------

static const char RANDOM_PAGE_BODY_NEW[] PROGMEM = R"HTML(
<style>
.rg{margin:1em 0}
.rg th{text-align:right;color:var(--muted);font-weight:normal;padding:.3em .7em .3em 0}
.rg td{padding:.3em .4em}
.rg input[type=number]{width:4em;padding:.3em .4em;text-align:center}
.rg button{min-width:6em;padding:.4em .8em;width:auto}
.rg .u{color:var(--muted);font-size:.9em}
.rg .o{display:inline-block;min-width:8em;min-height:1em;padding:.3em .6em;background:var(--card);border:1px solid var(--border);border-radius:4px;font-family:monospace;font-size:.92em;white-space:nowrap;overflow-x:auto;vertical-align:middle}
.rg .o:empty::before{content:"\00a0";color:var(--muted)}
.rg .o.flash{animation:rgflash .55s ease-out}
@keyframes rgflash{0%{background:var(--accent);color:#fff}100%{background:var(--card);color:inherit}}
</style>
<table class=rg>
<tr data-type=coin><th>Coin</th><td></td><td></td><td><button type=button onclick='r(this)'>Flip</button></td><td><span class=o></span></td></tr>
<tr data-type=dice data-arg=sides><th>Dice D</th><td><input type=number value=6 min=2 max=10000></td><td></td><td><button type=button onclick='r(this)'>Roll</button></td><td><span class=o></span></td></tr>
<tr data-type=password data-arg=len><th>Password</th><td><input type=number value=16 min=4 max=64></td><td class=u>chars</td><td><button type=button onclick='r(this)'>Generate</button></td><td><span class=o></span></td></tr>
<tr data-type=hex data-arg=n><th>Hex</th><td><input type=number value=16 min=1 max=32></td><td class=u>bytes</td><td><button type=button onclick='r(this)'>Generate</button></td><td><span class=o></span></td></tr>
</table>
<p class=muted style="margin-top:1.2em">Powered by accumulated radiation events.</p>
<script>
function r(b){
  var row=b.closest('tr');
  var t=row.dataset.type, k=row.dataset.arg;
  var i=row.querySelector('input'), o=row.querySelector('.o');
  o.classList.remove('flash');b.disabled=true;
  var u='/random.do?type='+t;
  if(i&&k)u+='&'+k+'='+i.value;
  fetch(u).then(r=>r.text()).then(x=>{
    o.textContent=x;void o.offsetWidth;o.classList.add('flash');
    b.disabled=false;
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
    const char* na = req.arg("n");
    long n = na ? atol(na) : 16;
    if (n < 1)  n = 16;
    if (n > 32) n = 32;
    uint8_t bytes[32];
    GRNG::extract(bytes, n);
    for (long i = 0; i < n; i++) {
      out[i*2]     = "0123456789abcdef"[bytes[i] >> 4];
      out[i*2 + 1] = "0123456789abcdef"[bytes[i] & 0xF];
    }
    outlen = n * 2;
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
