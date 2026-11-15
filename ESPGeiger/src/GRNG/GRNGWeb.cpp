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
#include "Corium/WebPortal/WebPortal.h"
#include "Corium/Util/Bytes.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"
#include <EGHttpServer.h>
#include "RANDOM_JS.gz.h"

static const char PW_CHARS[] PROGMEM = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static const char RANDOM_PAGE_BODY[] PROGMEM = R"HTML(
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
<script src=/rndjs)HTML" CORIUM_CACHE_BUST R"HTML(></script>
)HTML";

extern const char RANDOM_JS[] PROGMEM = R"JS(
[['Coin','coin',null,'Flip'],
['Dice D','dice',['sides',6,2,10000],'Roll'],
['Password','password',['len',16,4,64],'Generate','chars'],
['Hex','hex',['n',16,1,32],'Generate','bytes'],
['OTP','otp',['n',256,16,1024],'Download','bytes',1]
].forEach(c=>{
var t=document.createElement('tr'),
i=c[2]?'<input type=number aria-label="'+c[0]+'" value='+c[2][1]+' min='+c[2][2]+' max='+c[2][3]+'>':'',
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
)JS";

static void hRandomPage(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("Random"));
  res.sendChunk(FPSTR(RANDOM_PAGE_BODY));
  corium::WebPortal::endPage(res);
}

static void hRandomJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_RANDOM_JS
  corium::WebPortal::sendGz(res, FPSTR(HTTP_CT_JS), RANDOM_JS_GZ, RANDOM_JS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(RANDOM_JS));
#endif
}

// per_chunk <= 64
static void send_hex(EGHttpResponse& res, long n, size_t per_chunk) {
  res.beginChunked(200, FPSTR(HTTP_CT_PLAIN));
  uint8_t  chunk[64];
  char     hex[128];
  long left = n;
  while (left > 0) {
    size_t batch = left > (long)per_chunk ? per_chunk : (size_t)left;
    GRNG::extract(chunk, batch);
    hexEncode(hex, chunk, batch);
    res.sendChunk(hex, batch * 2);
    left -= batch;
  }
  res.endChunked();
}

static void random_otp(EGHttpRequest& req, EGHttpResponse& res) {
  int32_t n = 256;
  parseI32(req.arg(F("n")), &n);
  send_hex(res, clamp<int32_t>(n, 16, 1024), 64);
}

static void random_hex(EGHttpRequest& req, EGHttpResponse& res) {
  int32_t n = 16;
  parseI32(req.arg(F("n")), &n);
  if (n < 1)   n = 16;
  if (n > 256) n = 256;
  send_hex(res, n, 32);
}

static size_t random_coin(char* out) {
  uint8_t b;
  GRNG::extract(&b, 1);
  PGM_P s = (b & 1) ? PSTR("Heads") : PSTR("Tails");
  size_t len = strlen_P(s);
  memcpy_P(out, s, len);
  return len;
}

static size_t random_dice(EGHttpRequest& req, char* out, size_t cap) {
  int32_t sides = 6;
  parseI32(req.arg(F("sides")), &sides);
  if (sides < 2)     sides = 6;
  if (sides > 10000) sides = 10000;
  uint8_t bytes[4];
  GRNG::extract(bytes, 4);
  uint32_t r = rdBe32(bytes);
  long roll = (long)(r % (uint32_t)sides) + 1;
  return snprintf_P(out, cap, PSTR("%ld"), roll);
}

// out >= 64 chars
static size_t random_password(EGHttpRequest& req, char* out) {
  int32_t len = 16;
  parseI32(req.arg(F("len")), &len);
  if (len < 4)  len = 16;
  if (len > 64) len = 64;
  GRNG::pick(out, len, PW_CHARS, sizeof(PW_CHARS) - 1);
  return len;
}

// arg() buffer is shared, don't compare type after a match
static void hRandomDo(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* type = req.arg(F("type"));
  if (!type) { res.send(400, FPSTR(HTTP_CT_PLAIN), F("")); return; }

  if (strcmp_P(type, PSTR("otp")) == 0) { random_otp(req, res); return; }
  if (strcmp_P(type, PSTR("hex")) == 0) { random_hex(req, res); return; }

  char out[68];
  size_t len = 0;

  if (strcmp_P(type, PSTR("coin")) == 0) {
    len = random_coin(out);
  } else if (strcmp_P(type, PSTR("dice")) == 0) {
    len = random_dice(req, out, sizeof(out));
  } else if (strcmp_P(type, PSTR("password")) == 0) {
    len = random_password(req, out);
  } else {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F(""));
    return;
  }

  out[len] = '\0';
  res.send(200, FPSTR(HTTP_CT_PLAIN), out);
}

static const char R_RANDOM[] PROGMEM = "/random";
static const char R_DO[]     PROGMEM = "/random.do";
static const char R_RNDJS[]  PROGMEM = "/rndjs";

static const corium::WebRoute ROUTES[] PROGMEM = {
  {R_RANDOM, hRandomPage, EGHttpRequest::GET},
  {R_DO,     hRandomDo,   EGHttpRequest::GET},
  {R_RNDJS,  hRandomJs,   EGHttpRequest::GET},
};

void GRNGRoutes::registerRoutes(EGHttpServer& http) {
  corium::WebPortal::on(http, ROUTES, sizeof(ROUTES) / sizeof(ROUTES[0]));
}
