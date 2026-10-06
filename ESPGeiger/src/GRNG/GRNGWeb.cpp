/*
  GRNGWeb.cpp - /random page and handlers

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
#include "../Corium/WebPortal/WebPortal.h"
#include "../Corium/Util/StringUtil.h"
#include <EGHttpServer.h>
#include <string.h>
#include <stdlib.h>

static const char PW_CHARS[] PROGMEM = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

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
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("Random"));
  res.sendChunk(FPSTR(RANDOM_PAGE_BODY_NEW));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

// per_chunk <= 64
static void send_hex(EGHttpResponse& res, long n, size_t per_chunk) {
  res.beginChunked(200, FPSTR(HTTP_CT_PLAIN));
  uint8_t  chunk[64];
  char     hexbuf[128];
  long remaining = n;
  while (remaining > 0) {
    size_t batch = remaining > (long)per_chunk ? per_chunk : (size_t)remaining;
    GRNG::extract(chunk, batch);
    hex_encode(hexbuf, chunk, batch);
    res.sendChunk(hexbuf, batch * 2);
    remaining -= batch;
  }
  res.endChunked();
}

static void random_otp(EGHttpRequest& req, EGHttpResponse& res) {
  int32_t n = 256;
  parse_i32(req.arg(F("n")), &n);
  if (n < 16)   n = 16;
  if (n > 1024) n = 1024;
  send_hex(res, n, 64);
}

static void random_hex(EGHttpRequest& req, EGHttpResponse& res) {
  int32_t n = 16;
  parse_i32(req.arg(F("n")), &n);
  if (n < 1)   n = 16;
  if (n > 256) n = 256;
  send_hex(res, n, 32);
}

static size_t random_coin(char* out) {
  uint8_t b;
  GRNG::extract(&b, 1);
  PGM_P s = (b & 1) ? PSTR("Heads") : PSTR("Tails");
  size_t outlen = strlen_P(s);
  memcpy_P(out, s, outlen);
  return outlen;
}

static size_t random_dice(EGHttpRequest& req, char* out, size_t cap) {
  int32_t sides = 6;
  parse_i32(req.arg(F("sides")), &sides);
  if (sides < 2)     sides = 6;
  if (sides > 10000) sides = 10000;
  uint8_t bytes[4];
  GRNG::extract(bytes, 4);
  uint32_t r = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
               ((uint32_t)bytes[2] <<  8) |  (uint32_t)bytes[3];
  long roll = (long)(r % (uint32_t)sides) + 1;
  return snprintf_P(out, cap, PSTR("%ld"), roll);
}

// out >= 64 chars
static size_t random_password(EGHttpRequest& req, char* out) {
  int32_t len = 16;
  parse_i32(req.arg(F("len")), &len);
  if (len < 4)  len = 16;
  if (len > 64) len = 64;
  uint8_t bytes[64];
  GRNG::extract(bytes, len);
  for (long i = 0; i < len; i++) out[i] = pgm_read_byte(&PW_CHARS[bytes[i] % (sizeof(PW_CHARS) - 1)]);
  return len;
}

static uint32_t count_heads(long n) {
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
  return heads;
}

static size_t random_flip(EGHttpRequest& req, char* out, size_t cap) {
  // chi^2 1 dof, < 3.841 fair, < 6.635 suspect
  int32_t n = 1000;
  parse_i32(req.arg(F("n")), &n);
  if (n < 10)    n = 10;
  if (n > 10000) n = 10000;
  uint32_t heads = count_heads(n);
  uint32_t tails = (uint32_t)n - heads;
  float diff = (float)heads - (float)n / 2.0f;
  float chi2 = (diff * diff * 4.0f) / (float)n;
  PGM_P verdict = chi2 < 3.841f ? PSTR("fair")
                 : chi2 < 6.635f ? PSTR("suspect")
                                 : PSTR("biased");
  uint32_t chi2_milli = (uint32_t)(chi2 * 1000.0f + 0.5f);
  return snprintf_P(out, cap,
                    PSTR("N=%ld H=%lu T=%lu chi2=%lu.%03lu (%s)"),
                    (long)n, (unsigned long)heads, (unsigned long)tails,
                    (unsigned long)(chi2_milli / 1000UL),
                    (unsigned long)(chi2_milli % 1000UL),
                    verdict);
}

// arg() buffer is shared, don't compare type after a match
static void hRandomDo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* type = req.arg(F("type"));
  if (!type) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("")); return; }

  if (strcmp_P(type, PSTR("otp")) == 0) { random_otp(req, res); return; }
  if (strcmp_P(type, PSTR("hex")) == 0) { random_hex(req, res); return; }

  char out[68];
  size_t outlen = 0;

  if (strcmp_P(type, PSTR("coin")) == 0) {
    outlen = random_coin(out);
  } else if (strcmp_P(type, PSTR("dice")) == 0) {
    outlen = random_dice(req, out, sizeof(out));
  } else if (strcmp_P(type, PSTR("password")) == 0) {
    outlen = random_password(req, out);
  } else if (strcmp_P(type, PSTR("flip")) == 0) {
    outlen = random_flip(req, out, sizeof(out));
  } else {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F(""));
    return;
  }

  out[outlen] = '\0';
  res.send(200, FPSTR(HTTP_CT_PLAIN), out);
}

void GRNGRoutes::registerRoutes(EGHttpServer& http) {
  http.on(F("/random"),    EGHttpRequest::GET, hRandomPage);
  http.on(F("/random.do"), EGHttpRequest::GET, hRandomDo);
}
