/*
  CounterWeb.cpp - Counter HTTP routes (/json, /clicks, /hist, /pause, ...)

  Split from Counter.cpp: request-cold code kept out of the TU that holds
  the hot counting loop (icache locality).

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
#include <Arduino.h>
#include <EGHttpServer.h>
#include <EGJson.h>
#include "Counter.h"
#include "Corium/Module/ExternalPause.h"
#include "Corium/Measure/EGMeasure.h"
#include "Corium/Prefs/EGPrefs.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Net/Wifi.h"
#include "Corium/Util/Words.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/TickProfile.h"
#include "Corium/System/Clock.h"
#include "Corium/System/Uptime.h"
#include "Corium/WebPortal/WebPortal.h"
#include "HIST_JS.gz.h"
#include "JS_BUNDLE.gz.h"
#if defined(ESP8266) && defined(EG_TIMER1) && defined(EG_TIMER1_STATS)
#include <EGTimer1.h>
#endif

namespace CounterRoutes { void registerRoutes(EGHttpServer& http); }

extern const char picographJS[] PROGMEM;
extern const char statusJS[]    PROGMEM;

// no %llu in nano libc
static void fmt_u64(char* out, size_t cap, uint64_t v) {
  char digits[24];
  int n = 0;
  if (v == 0) { digits[n++] = '0'; }
  else { while (v > 0 && n < (int)sizeof(digits)) { digits[n++] = (char)('0' + v % 10); v /= 10; } }
  size_t o = 0;
  while (n > 0 && o + 1 < cap) out[o++] = digits[--n];
  out[o] = 0;
}

// snprintf returns the untruncated length
static void send_n(EGHttpResponse& res, const char* line, size_t cap, int n) {
  if (n <= 0) return;
  res.sendChunk(line, (size_t)n < cap ? (size_t)n : cap - 1);
}

static void clicks_day(EGHttpResponse& res, char* line, size_t cap) {
  send_n(res, line, cap, snprintf_P(line, cap,
    PSTR("{\"last_day\":[%d"), (int)gcounter.clicks_hour));

  const int histSize = gcounter.day_hourly_history.size();
  for (int i = histSize; i > 0; i--) {
    send_n(res, line, cap, snprintf_P(line, cap, PSTR(",%d"),
                                 (int)gcounter.day_hourly_history[i - 1]));
  }

  send_n(res, line, cap, snprintf_P(line, cap,
    PSTR("],\"today\":%d,\"yesterday\":%d,\"ratio\":\"%s\""),
    (int)gcounter.clicks_today,
    (int)gcounter.clicks_yesterday,
    EGPrefs::getString("counter", EG_K_RATIO)));
}

static void clicks_since(EGHttpResponse& res, char* line, size_t cap) {
  int n;
  if (Clock::synced) {
    n = snprintf_P(line, cap,
      PSTR(",\"start\":%lu"), (unsigned long)Clock::boot_epoch);
  } else {
    unsigned long uptime = Uptime::seconds() - start;
    n = snprintf_P(line, cap,
      PSTR(",\"uptime\":%lu"), uptime);
  }
  send_n(res, line, cap, n);
}

static void clicks_life(EGHttpResponse& res, char* line, size_t cap) {
  char clkBuf[24];
  fmt_u64(clkBuf, sizeof(clkBuf), gcounter.get_lifetime_clicks_total());
  send_n(res, line, cap, snprintf_P(line, cap,
    PSTR(",\"life\":{\"clk\":%s,\"fbt\":%lu,\"secs\":%lu}"),
    clkBuf, (unsigned long)gcounter.get_first_boot_ts(),
    gcounter.get_lifetime_seconds()));
}

// log2 buckets, 64 us to 512 s+
static void clicks_hist(EGHttpResponse& res, char* line, size_t cap) {
  const uint32_t* hist = gcounter.pulse_histogram();
  const uint8_t   nb   = Counter::pulse_histogram_buckets();
  send_n(res, line, cap, snprintf_P(line, cap,
                               PSTR(",\"hist\":[%lu"), (unsigned long)hist[0]));
  for (uint8_t b = 1; b < nb; b++) {
    send_n(res, line, cap, snprintf_P(line, cap, PSTR(",%lu"),
                                 (unsigned long)hist[b]));
  }
  res.sendChunk(F("]"));
  uint32_t mp = gcounter.get_min_pulse_us();
  if (mp > 0) {
    send_n(res, line, cap, snprintf_P(line, cap,
                                 PSTR(",\"min_pulse_us\":%lu"), (unsigned long)mp));
  }
}

static void hClicks(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  char line[160];
  clicks_day(res, line, sizeof(line));
  gcounter.input()->appendClicksExtra(res);
  clicks_since(res, line, sizeof(line));
  if (gcounter.get_lifetime_enabled()) clicks_life(res, line, sizeof(line));
  clicks_hist(res, line, sizeof(line));
  res.sendChunk(F("}"));
  res.endChunked();
}

static void hLastData(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // GeigerLog field order, keep it
  // CPM,CPS,CPM1st,CPS1st,CPM2nd,CPS2nd,CPM3rd,CPS3rd,Temp,Press,Humid,Xtra
  char cpm[16], cps[16], cpm5[16], cpm15[16], t[16], h[16], p[16], xtra[16];
  egm_text(PSTR("cpm"),   EGM_OUT_JSON, cpm,   sizeof(cpm));
  egm_text(PSTR("cps"),   EGM_OUT_JSON, cps,   sizeof(cps));
  egm_text(PSTR("cpm5"),  EGM_OUT_JSON, cpm5,  sizeof(cpm5));
  egm_text(PSTR("cpm15"), EGM_OUT_JSON, cpm15, sizeof(cpm15));
  egm_text(PSTR("t"),     EGM_OUT_JSON, t,     sizeof(t));
  egm_text(PSTR("h"),     EGM_OUT_JSON, h,     sizeof(h));
  egm_text(PSTR("p"),     EGM_OUT_JSON, p,     sizeof(p));
  if (!egm_text(PSTR("hv"), EGM_OUT_JSON, xtra, sizeof(xtra)))
    egm_text(PSTR("usv"), EGM_OUT_JSON, xtra, sizeof(xtra));

  char out[128];
  snprintf_P(out, sizeof(out),
    PSTR("%s, %s, %s, , %s, , , , %s, %s, %s, %s"),
    cpm, cps, cpm5, cpm15, t, p, h, xtra);
  res.send(200, FPSTR(HTTP_CT_PLAIN), out);
}

#if GEIGER_IS_TEST(GEIGER_TYPE)
static void hSetCPM(EGHttpRequest& req, EGHttpResponse& res, void*) {
  int32_t target;
  if (parse_i32(req.arg(F("v")), &target) && target > 0) gcounter.set_target_cpm(target);
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
}
#endif

#if defined(ESP8266) && defined(EG_TIMER1) && defined(EG_TIMER1_STATS)
// diag only, not a measurement
static void json_timer1_stats(EGHttpResponse& res) {
  char buf[200];
  int n;
  for (uint8_t i = 0; (n = EGTimer1::stats_json(buf, sizeof(buf), i)) > 0; i++) {
    if ((size_t)n < sizeof(buf)) res.sendChunk(buf, (size_t)n);
  }
}
#endif

static void send_part(void* arg, const char* s, size_t n) {
  static_cast<EGHttpResponse*>(arg)->sendChunk(s, n);
}

static void hJson(EGHttpRequest& req, EGHttpResponse& res, void*) {
  bool raw = req.arg(F("raw")) != nullptr;
  res.beginChunked(200, FPSTR(HTTP_CT_JSON));
  char out[256];
  EGJson j(out, sizeof(out), send_part, &res);
  j.map_begin();
  egm_json_display(j, raw);
  j.flush();   // closed by hand
#if defined(ESP8266) && defined(EG_TIMER1) && defined(EG_TIMER1_STATS)
  if (raw) json_timer1_stats(res);
#endif
  res.sendChunk(F("}"));
  res.endChunked();
}

static const char HISTORY_BODY_LIFETIME[] PROGMEM = R"HTML(
<div id=lf style=display:none><h2>Lifetime</h2><div class=card><div id=g2>
<div><span class=muted>Clicks </span><b id=lfc>&mdash;</b></div>
<div><span class=muted>Dose </span><b id=lfs class=dose>&mdash;</b></div>
<div><span class=muted>Tracked </span><b id=lfd>&mdash;</b></div>
<div><span class=muted>Install age </span><b id=lfi>&mdash;</b></div>
<div><span class=muted>Avg CPM </span><b id=lfa>&mdash;</b></div>
</div><p style="margin:.8em 0 0"><button class=danger onclick="if(confirm('Reset lifetime counters?'))fetch('/life/reset',{method:'POST'}).then(()=>location.reload())">Reset lifetime</button></p></div></div>
)HTML";

static const char HISTORY_BODY_INTERPULSE[] PROGMEM = R"HTML(
<h2>Inter-pulse intervals</h2>
<div class=card style="margin:.4em 0">
<div id=ph class=bar-row></div>
<div id=phL class=bar-lbls></div>
<div class=muted style="font-size:.85em;margin-top:.4em">log<sub>2</sub> buckets 64&micro;s to &ge;512s &middot; cumulative since boot<span id=mpW style=display:none> &middot; observed min <b id=mp>&mdash;</b></span></div>
</div>
)HTML";

static const char HISTORY_BODY_LAST24[] PROGMEM = R"HTML(
<h2>Last 24 hours</h2>
<div class=card style="margin:.4em 0"><div id=g2>
<div><span class=muted>Today </span><b id=hsT>&mdash;</b></div>
<div><span class=muted>Yesterday </span><b id=hsY>&mdash;</b></div>
<div><span class=muted id=hsAL>24h avg CPM </span><b id=hsA>&mdash;</b></div>
<div><span class=muted>24h peak CPM </span><b id=hsP>&mdash;</b></div>
<div><span class=muted>Day rate vs yesterday </span><b id=hsD>&mdash;</b></div>
</div></div>
<div id=bc class="bar-row bot"></div>
<table>
<thead><tr><th>Date</th><th>Clicks</th><th>Avg CPM</th><th><span class=usvL>&micro;Sv</span></th></tr></thead>
<tbody id=tb></tbody>
</table>
<script src=/histjs)HTML" EG_CACHE_BUST R"HTML(></script>
)HTML";

static const char HIST_JS[] PROGMEM = R"JS(
fetch('/clicks').then(r=>r.json()).then(o=>{
  var $=byID,N=Date.now()/1000|0,T=o.today||0,Y=o.yesterday||0,
      tb=$('tb'),
      start='start' in o?new Date(o.start*1000):new Date(Date.now()-o.uptime*1000),
      rlv='roll' in o?o.roll*1000:3600000,
      rows='',mx=0,sum=0,
      F=s=>s>=31536000?(s/31536000).toFixed(1)+'y':s>=86400?(s/86400|0)+'d':s>=3600?(s/3600).toFixed(1)+'h':s>=60?(s/60|0)+'m':s+'s';
  if('life' in o){
    var L=o.life,ts=L.secs>0?L.secs:(L.fbt>0?N-L.fbt:0);
    $('lfc').textContent=L.clk.toLocaleString();
    setDose($('lfs'),L.clk/60/o.ratio);
    if(ts>0){$('lfd').textContent=F(ts);$('lfa').textContent=(L.clk*60/ts).toFixed(1)}
    if(L.fbt>0)$('lfi').textContent=F(N-L.fbt);
    $('lf').style.display='';
  }
  var nowMs=Date.now(),curB=Math.floor(nowMs/rlv)*rlv,pk=0,bs=o.start*1000;
  o.last_day.forEach((n,i)=>{
    sum+=n;if(n>mx)mx=n;
    var sd=curB-i*rlv,ed=i?sd+rlv:nowMs;
    if(i===o.last_day.length-1&&sd<bs)sd=bs;
    var ms=ed-sd;
    if(ms>0){var r=n*60000/ms;if(r>pk)pk=r}
  });
  $('hsT').textContent=T.toLocaleString();
  $('hsY').textContent=Y.toLocaleString();
  var es='start' in o?Math.max(0,N-o.start):o.uptime||0,
      h60=rlv>=3600000&&es>=3600,
      cpmA=h60?((o.last_day[1]||0)*(60-new Date().getMinutes())/60+(o.last_day[0]||0))/60
              :es>0?sum*60/Math.min(es,86400):0,
      M=N%86400,
      ok=Y>0&&M>=3600&&'start' in o&&o.start<=N-M-86400,
      Tp=M>0?T*86400/M:T,
      p=ok?(Tp-Y)*100/Y:0;
  $('hsA').textContent=Math.round(cpmA);
  $('hsAL').textContent=(h60?'1h ':'')+'avg CPM ';
  $('hsP').textContent=Math.round(pk);
  $('hsD').textContent=ok?(p>=0?'+':'')+p.toFixed(1)+'%':'—';
  if(mx<1)mx=1;
  var bars='';
  for(var i=23;i>=0;i--){
    var v=o.last_day[i]||0,
        h=v?(v/mx*100).toFixed(1)+'%':'3px',
        bg=v?'var(--accent)':'var(--border)';
    bars+='<div class=bar-cell style="background:'+bg+';height:'+h+';min-height:3px" title="'+v+'"></div>';
  }
  byID('bc').innerHTML=bars;
  o.last_day.forEach(function(n,idx){
    var off=idx*rlv,
        sd=new Date(Math.floor(Date.now()/rlv)*rlv-off),
        ed=new Date(sd.getTime()+rlv);
    if(idx==0)ed=new Date();
    if(idx==o.last_day.length-1&&sd<start)sd=start;
    var mins=(ed-sd)/60000,
        ok=mins>0&&o.ratio>0,
        uv=ok?n/mins/o.ratio:0;
    rows+='<tr><td>'+sd.toLocaleString()+'</td><td>'+n+'</td><td>'+(ok?Math.ceil(n/mins):'—')+'</td><td'+(ok?' class=usv data-uv="'+uv+'"':'')+'>'+(ok?uv.toFixed(4):'—')+'</td></tr>';
  });
  tb.innerHTML=rows;
  if(o.hist&&o.hist.length){
    var H=o.hist,mh=1,
        L=['<64us','<128us','<256us','<512us','<1ms','<2ms','<4ms','<8ms','<16ms','<32ms','<64ms','<128ms','<256ms','<512ms','<1s','<2s','<4s','<8s','<16s','<32s','<1m','<2m','<4m','<8m','>8m'],
        showL=[0,4,8,12,16,20,24];
    H.forEach(v=>{if(v>mh)mh=v});
    var hb='',lb='';
    for(var i=0;i<H.length;i++){
      var v=H[i],hp=v?(v/mh*100).toFixed(1)+'%':'2px',
          bg=v?'var(--accent)':'var(--border)',
          ta=i===0?'left':i===H.length-1?'right':'center';
      hb+='<div class=bar-cell style="background:'+bg+';height:'+hp+';min-height:2px" title="'+L[i]+': '+v+'"></div>';
      lb+='<div class=bar-cell style="text-align:'+ta+'">'+(showL.indexOf(i)>=0?L[i]:'')+'</div>';
    }
    $('ph').innerHTML=hb;
    $('phL').innerHTML=lb;
  }
  if(o.min_pulse_us){
    var u=o.min_pulse_us,
        s=u<1000?u+'µs':u<1e6?(u/1000).toFixed(u<10000?2:1)+'ms':(u/1e6).toFixed(2)+'s';
    $('mp').textContent=s;
    $('mpW').style.display='';
  }
  if(window.applyRad)applyRad();
});
)JS";

static void hHistory(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  const char* fn = DeviceInfo::friendlyName();
  const char* sub = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  WebPortal::sendPageHead(res, F("History"), sub);
  res.sendChunk(FPSTR(HISTORY_BODY_LIFETIME));
#if !GEIGER_IS_UDPRX(GEIGER_TYPE)
  // udp rx is batched, no intervals
  res.sendChunk(FPSTR(HISTORY_BODY_INTERPULSE));
#endif
  res.sendChunk(FPSTR(HISTORY_BODY_LAST24));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hHistJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if EG_GZ_HIST_JS
  res.sendGzipP(200, FPSTR(HTTP_CT_JS), HIST_JS_GZ, HIST_JS_GZ_LEN);
#else
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(HIST_JS));
#endif
}

static void hLifeReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  gcounter.reset_lifetime();
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
}

// /pause?s=N, 0 resumes, max 24 h
static void hPause(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* sec = req.arg(F("s"));
  if (sec && !ExternalPause::start_s(sec)) {
    res.send(400, FPSTR(HTTP_CT_PLAIN), F("s is seconds, 0 resumes"));
    return;
  }
  char body[40];
  uint32_t rem = ExternalPause::remaining_ms();
  snprintf_P(body, sizeof(body),
             rem ? PSTR("PAUSED %u s") : PSTR("RESUMED"), (unsigned)(rem / 1000U));
  res.send(200, FPSTR(HTTP_CT_PLAIN), body);
}

// /status pollers, /json and /cs
#if !EG_GZ_JS_BUNDLE
extern const char statusJS[] PROGMEM = R"JS(
!function(){var $=byID,B=$('blip'),U=$('upt'),C=$('cpm'),T=$('tc'),V=$('usv'),S=$('cs'),R=$('rssi'),W=$('tw'),D=Date,X=XMLHttpRequest,O=setTimeout,P=n=>String(n).padStart(2,"0"),e=new Graph("g1",["CPM","CPM5","CPM15"],"cpm","g2",5,null,0,!0,!0,5,15);e.av=3;
if(W&&window.GPIN>=0)W.innerHTML+=' (GPIO '+window.GPIN+')';
if(window.BO)$('bw').style.display='';
var ac,cps=0,nb,mu=$('snd'),mt=1,AC=window.AudioContext||window.webkitAudioContext;
var LS=localStorage;
function applySnd(){var s=LS.sndBtn,c=document.documentElement.classList.contains('crt');mu.style.display=(s==='1'||(c&&s!=='0'))?'':'none'}
applySnd();window.applySnd=applySnd;
function ensureAC(){if(!mt&&!ac&&AC){ac=new AC();var N=Math.floor(ac.sampleRate*.008);nb=ac.createBuffer(1,N,ac.sampleRate);var d=nb.getChannelData(0),sd=2166136261>>>0,cid=window.CHIPID||'0';for(var i=0;i<cid.length;i++)sd=Math.imul(sd^cid.charCodeAt(i),16777619)>>>0;if(!sd)sd=1;for(var i=0;i<N;i++){sd^=sd<<13;sd^=sd>>>17;sd^=sd<<5;d[i]=((sd&65535)/32768-1)*Math.exp(-i*.04)}}if(ac&&ac.resume)ac.resume()}
mu.onclick=function(){mt=!mt;mu.textContent='Sound: '+(mt?'off':'on');ensureAC()};
window.toggleTick=()=>{LS.sndBtn=LS.sndBtn==='1'?'0':'1';applySnd()};
(function clk(){var n=500;
if(cps>0&&!mt&&ac){
var t=ac.currentTime,s=ac.createBufferSource(),f=ac.createBiquadFilter(),g=ac.createGain();
s.buffer=nb;f.type='bandpass';f.frequency.value=1200;f.Q.value=2;g.gain.value=1.1;
s.connect(f);f.connect(g);g.connect(ac.destination);s.start(t);
n=Math.max(20,-Math.log(1-Math.random())/cps*1000)}
O(clk,n)})();
if(window._seedHist&&window._seedHist.length){var N=window._seedHist.length,w=D.now();window._seedHist.forEach((v,i)=>e.update([v,v,v],new D(w-(N-1-i)*1e3).toLocaleTimeString()))}
var J,L=0,I=2e3,K='#0f0',A=0,F=function(){B.classList.add('f');O(()=>B.classList.remove('f'),90)};
B.style.setProperty('--blip',K);
setInterval(function(){A+=100/I;if(A>=1){F();if((A-=1)>3)A=3}},100);
var Q=function(){if(!window._csk){window._csk=1;O(f,100)}},t=function(){var n=new X;n.open("GET","/json",!0);
n.onload=function(){if(n.status>=200&&n.status<400){var o=JSON.parse(n.responseText),u=o.ut;
U.textContent=(u/86400|0)+"T"+P((u/3600|0)%24)+":"+P((u/60|0)%60)+":"+P(u%60);
var dc=o.c<10?o.c5:o.c;C.textContent=dc.toFixed(2);T.textContent=o.tc;setUsv(V,dc/o.r);S.textContent=o.cs.toFixed(2);cps=o.cs;
var v=o.rssi,p=v<=-100?0:v>=-50?100:2*(v+100);R.textContent=v+' dBm ('+p+'%)';
if(W)W.style.display=(o.tc===0&&u>=Math.max(60,6e3/o.r))||o.tube===0?'':'none';
if(o.t!=null||o.h!=null||o.p!=null){var tu=o.tu|0,us=['\xb0C','\xb0F','K'],ps=[];if(o.t!=null)ps.push(['Temp',o.t.toFixed(1)+us[tu]]);if(o.h!=null)ps.push(['Humid',o.h.toFixed(0)+'%']);if(o.p!=null)ps.push(['Press',o.p.toFixed(1)+' hPa']);var fx=ps.length>2;$('envR').className=fx?'row envflex':'row';$('envR').innerHTML=ps.map(fx?function(p){return '<div><b>'+p[0]+'</b>'+p[1]+'</div>'}:function(p){return '<b>'+p[0]+'</b><span>'+p[1]+'</span>'}).join('');$('envR').style.display=''}else $('envR').style.display='none';
e.update([o.c,o.c5,o.c15]);var r=o.c5>0&&o.c>0?o.c/o.c5:1;
I=Math.max(100,Math.min(4e3,2e3/r));
var z=(o.c-o.c5)/Math.sqrt(Math.max(o.c5,1e-6));
K=o.c<.01&&o.c5<.01?'#08f':z>3?'#f33':z>1.5?'#fa0':z<-1.5?'#c0f':'#0f0';
B.style.setProperty('--blip',K)}L=D.now();Q();J=O(t,1e3)};
n.onerror=function(){L=D.now();Q();J=O(t,6e3)};n.send()};
O(t,250);document.addEventListener("visibilitychange",function(){if(!document.hidden){clearTimeout(J);J=O(t,Math.max(0,1e3-(D.now()-L)))}})}();
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
window._boost=function(){ci=3e3;clearTimeout(lt);lt=setTimeout(f,200)};
)JS";
#endif

// light canvas, picograph has no theme
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
<p id=tw class="a aw" style=display:none><b>&#9888; No counts detected</b> - check tube and input wiring.</p>
<p id=bw class="a aw" style=display:none><b>&#9888; Brownout reset detected</b> - check the power supply.</p>
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
<button type=submit>Send</button>
</form>
</details>
<script>function cmdSend(e){e.preventDefault();var i=byID('ci'),v=i.value.trim();if(!v)return!1;var h=2166136261;for(var k=0;k<v.length;k++)h=Math.imul(h^v.charCodeAt(k),16777619)>>>0;if(h==1532375204)byID('snd').click();else if(h==4098154876)toggleCrt();else{fetch('/webcmd',{method:'POST',body:v});if(window._boost)_boost()}i.value='';return!1}</script>
<script src=/js)HTML" EG_CACHE_BUST R"HTML(></script>
)HTML";

static void status_seed(EGHttpResponse& res) {
  int bo = DeviceInfo::resetReason() == DeviceInfo::RR_BROWNOUT;
  char buf[400];
  size_t sp = 0;
  advance_pos(sp, snprintf_P(buf, sizeof(buf),
              PSTR("<script>var w=window;w.CHIPID=\"%s\";w.GPIN=%d;w.BO=%d;w._seedHist=["),
              DeviceInfo::chipid(), gcounter.input_pin(), bo), sizeof(buf));
  int histSize = gcounter.cpm_history.size();
  for (int i = 0; i < histSize && sp + sizeof(",-2147483648") <= sizeof(buf); i++) {
    advance_pos(sp, snprintf_P(buf + sp, sizeof(buf) - sp, i ? PSTR(",%d") : W_FMT_D,
                gcounter.cpm_history[i]), sizeof(buf));
  }
  res.sendChunk(buf, sp);
  res.sendChunk(F("]</script>"));
}

static void hStatus(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  const char* fn = DeviceInfo::friendlyName();
  const char* sub = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  WebPortal::sendPageHead(res, F("Status"), sub);
  status_seed(res);
  res.sendChunk(FPSTR(STATUS_BODY));
  WebPortal::sendPageTail(res, true);
  res.endChunked();
}

static void hJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if EG_GZ_JS_BUNDLE
  res.sendGzipP(200, FPSTR(HTTP_CT_JS), JS_BUNDLE_GZ, JS_BUNDLE_GZ_LEN);
#else
  res.beginChunked(200, FPSTR(HTTP_CT_JS));
  res.sendChunk(FPSTR(picographJS));
  res.sendChunk(FPSTR(statusJS));
  res.endChunked();
#endif
}

void CounterRoutes::registerRoutes(EGHttpServer& http) {
  http.on(F("/status"),   EGHttpRequest::GET,  hStatus);
  http.on(F("/js"),       EGHttpRequest::GET,  hJs);
  http.on(F("/clicks"),   EGHttpRequest::GET,  hClicks);
  http.on(F("/lastdata"), EGHttpRequest::GET,  hLastData);
  http.on(F("/json"),     EGHttpRequest::GET,  hJson);
  http.on(F("/hist"),     EGHttpRequest::GET,  hHistory);
  http.on(F("/histjs"),   EGHttpRequest::GET,  hHistJs);
  http.on(F("/life/reset"), EGHttpRequest::POST, hLifeReset);
  http.on(F("/pause"),      EGHttpRequest::GET,  hPause);
#if GEIGER_IS_TEST(GEIGER_TYPE)
  http.on(F("/cpm"),      EGHttpRequest::GET,  hSetCPM);
#endif
}
