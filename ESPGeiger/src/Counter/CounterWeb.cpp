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
#include "Counter.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/StringUtil.h"
#include "../Util/DeviceInfo.h"
#include "../Util/Wifi.h"
#include "../Util/FastMillis.h"
#include "../Util/TickProfile.h"
#include "../WebPortal/WebPortal.h"
#include "../EnvSensor/EnvSensor.h"
#include "HIST_JS.gz.h"
#ifdef ESPG_HV_ADC
#include "../HV/HV.h"
#endif

// ---------- HTTP routes ----------

extern Counter gcounter;

namespace CounterRoutes { void registerRoutes(EGHttpServer& http); }

// uint64 -> decimal ASCII (nano-libc has no %llu).
static void fmt_u64(char* out, size_t cap, uint64_t v) {
  char tmp[24];
  int n = 0;
  if (v == 0) { tmp[n++] = '0'; }
  else { while (v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + v % 10); v /= 10; } }
  size_t o = 0;
  while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
  out[o] = 0;
}

static void hClicks(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // /clicks JSON can run ~250 B with full 24h history; chunked to be safe.
  res.beginChunked(200, "application/json");

  char line[160];
  int n;

  n = snprintf_P(line, sizeof(line),
    PSTR("{\"last_day\":[%d"), (int)gcounter.clicks_hour);
  if (n > 0) res.sendChunk(line, (size_t)n);

  const int histSize = gcounter.day_hourly_history.size();
  for (int i = histSize; i > 0; i--) {
    n = snprintf_P(line, sizeof(line), PSTR(",%d"),
                   (int)gcounter.day_hourly_history[i - 1]);
    if (n > 0) res.sendChunk(line, (size_t)n);
  }

  n = snprintf_P(line, sizeof(line),
    PSTR("],\"today\":%d,\"yesterday\":%d,\"ratio\":\"%s\""),
    (int)gcounter.clicks_today,
    (int)gcounter.clicks_yesterday,
    EGPrefs::getString("sys", "ratio"));
  if (n > 0) res.sendChunk(line, (size_t)n);

  gcounter.input()->appendClicksExtra(res);

  if (ntpclient.synced) {
    n = snprintf_P(line, sizeof(line),
      PSTR(",\"start\":%lu"), (unsigned long)ntpclient.boot_epoch);
  } else {
    unsigned long uptime = ntpclient.getUptime() - start;
    n = snprintf_P(line, sizeof(line),
      PSTR(",\"uptime\":%lu"), uptime);
  }
  if (n > 0) res.sendChunk(line, (size_t)n);

  if (gcounter.get_lifetime_enabled()) {
    char clkBuf[24];
    fmt_u64(clkBuf, sizeof(clkBuf), gcounter.get_lifetime_clicks_total());
    n = snprintf_P(line, sizeof(line),
      PSTR(",\"life\":{\"clk\":%s,\"fbt\":%lu,\"secs\":%lu}"),
      clkBuf, (unsigned long)gcounter.get_first_boot_ts(),
      gcounter.get_lifetime_seconds());
    if (n > 0) res.sendChunk(line, (size_t)n);
  }

  // Inter-pulse-interval histogram (log2 buckets, 64us .. >=512s).
  {
    const uint32_t* hist = gcounter.pulse_histogram();
    const uint8_t   nb   = Counter::pulse_histogram_buckets();
    n = snprintf_P(line, sizeof(line),
                   PSTR(",\"hist\":[%lu"), (unsigned long)hist[0]);
    if (n > 0) res.sendChunk(line, (size_t)n);
    for (uint8_t b = 1; b < nb; b++) {
      n = snprintf_P(line, sizeof(line), PSTR(",%lu"),
                     (unsigned long)hist[b]);
      if (n > 0) res.sendChunk(line, (size_t)n);
    }
    res.sendChunk(F("]"));
    // Smallest inter-pulse interval since boot, paired with the histogram.
    uint32_t mp = gcounter.get_min_pulse_us();
    if (mp > 0) {
      n = snprintf_P(line, sizeof(line),
                     PSTR(",\"min_pulse_us\":%lu"), (unsigned long)mp);
      if (n > 0) res.sendChunk(line, (size_t)n);
    }
  }

  res.sendChunk(F("}"));
  res.endChunked();
}

static void hLastData(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // GeigerLog WiFiServer 12-field order:
  //   CPM,CPS,CPM1st,CPS1st,CPM2nd,CPS2nd,CPM3rd,CPS3rd,Temp,Press,Humid,Xtra
  // Absent fields left empty (GeigerLog reads "" as missing).
  char cpm[16], cps[16], cpm5[16], cpm15[16];
  format_f(cpm,   sizeof(cpm),   gcounter.get_cpmf());
  format_f(cps,   sizeof(cps),   gcounter.get_cps());
  format_f(cpm5,  sizeof(cpm5),  gcounter.get_cpm5f());
  format_f(cpm15, sizeof(cpm15), gcounter.get_cpm15f());

  char t[16] = "", h[16] = "", p[16] = "";
  if (envsensor.present()) {
    float et = envsensor.tempC(), eh = envsensor.humidity(), ep = envsensor.pressure();
    if (!isnan(et)) format_f(t, sizeof(t), et);
    if (!isnan(eh)) format_f(h, sizeof(h), eh);
    if (!isnan(ep)) format_f(p, sizeof(p), ep);
  }

  char xtra[16];   // HV on HW builds, else dose rate
#ifdef ESPG_HV_ADC
  if (hv.get_pwm_pin() >= 0 && hv.get_vd_ratio() != 0)
    format_f(xtra, sizeof(xtra), hv.hvReading.get());
  else
#endif
    format_f(xtra, sizeof(xtra), gcounter.get_usv());

  char out[128];
  snprintf_P(out, sizeof(out),
    PSTR("%s, %s, %s, , %s, , , , %s, %s, %s, %s"),
    cpm, cps, cpm5, cpm15, t, p, h, xtra);
  res.send(200, "text/plain", out);
}

#if GEIGER_IS_TEST(GEIGER_TYPE)
static void hSetCPM(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* v = req.arg("v");
  if (v) {
    int target = atoi(v);
    if (target > 0) gcounter.set_target_cpm(target);
  }
  res.send(200, "text/plain", "OK");
}
#endif

static void hJson(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // Live status as JSON. Chunked since ~250-320 B exceeds the inline-send cap.
  bool raw = req.arg("raw") != nullptr;  // ?raw=1: un-smoothed heap + frag/lfb
  res.beginChunked(200, "application/json");
  char buf[200];
  char c[16], s[16], c5[16], c15[16], cs[16];
  format_f(c,   sizeof(c),   gcounter.get_cpmf());
  format_f(s,   sizeof(s),   gcounter.get_usv());
  format_f(c5,  sizeof(c5),  gcounter.get_cpm5f());
  format_f(c15, sizeof(c15), gcounter.get_cpm15f());
  format_f(cs,  sizeof(cs),  gcounter.get_cps());
  int n = snprintf_P(buf, sizeof(buf),
    PSTR("{\"ut\":%lu,\"c\":%s,\"s\":%s,\"c5\":%s,\"c15\":%s,\"cs\":%s,\"r\":%s,\"tc\":%u,\"mem\":%u,\"rssi\":%d"),
    DeviceInfo::uptime(),
    c, s, c5, c15, cs,
    EGPrefs::getString("sys", "ratio"),
    gcounter.total_clicks,
    raw ? ESP.getFreeHeap() : DeviceInfo::freeHeap(),
    (int)Wifi::rssi);
  if (n > 0) res.sendChunk(buf, (size_t)n);
  n = snprintf_P(buf, sizeof(buf),
    PSTR(",\"tube\":%u,\"sat\":%u"),
    (unsigned)(gcounter.get_tube_alive() ? 1 : 0),
    (unsigned)(gcounter.get_saturated() ? 1 : 0));
  if (n > 0) res.sendChunk(buf, (size_t)n);
#ifdef ESPG_HV_ADC
  char hvBuf[16];
  format_f(hvBuf, sizeof(hvBuf), hv.hvReading.get());
  n = snprintf_P(buf, sizeof(buf), PSTR(",\"hv\":%s"), hvBuf);
  if (n > 0) res.sendChunk(buf, (size_t)n);
#endif
  // /json is the live-display endpoint used by /status JS. Temperature
  // ships in the user's preferred unit so the JS doesn't have to convert.
  // External APIs (MQTT/WebAPI/webhook/UDP) still send canonical units.
  if (envsensor.present()) {
    float et = envsensor.tempUser(), eh = envsensor.humidity(), ep = envsensor.pressure();
    char fbuf[12];
    if (!isnan(et)) {
      format_f(fbuf, sizeof(fbuf), et);
      n = snprintf_P(buf, sizeof(buf), PSTR(",\"t\":%s,\"tu\":%u"), fbuf, envsensor.unit());
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
    if (!isnan(eh)) {
      format_f(fbuf, sizeof(fbuf), eh);
      n = snprintf_P(buf, sizeof(buf), PSTR(",\"h\":%s"), fbuf);
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
    if (!isnan(ep)) {
      format_f(fbuf, sizeof(fbuf), ep);
      n = snprintf_P(buf, sizeof(buf), PSTR(",\"p\":%s"), fbuf);
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
  }
  gcounter.input()->appendJsonExtra(res);
  // tick/t_max/lps + heap diagnostics are not used by the status page; only
  // emit under ?raw=1 to keep the polled /json lean.
  if (raw) {
    n = snprintf_P(buf, sizeof(buf),
      PSTR(",\"tick\":%u,\"t_max\":%u,\"lps\":%u,\"frag\":%u,\"lfb\":%u,\"lfblow\":%u"),
      TickProfile::tick_us, TickProfile::tick_max_us, TickProfile::lps_raw,
      (unsigned)DeviceInfo::heapFrag(),
      (unsigned)DeviceInfo::largestFreeBlock(),
      (unsigned)DeviceInfo::largestFreeBlockLow());
    if (n > 0) res.sendChunk(buf, (size_t)n);
    if (gcounter.get_cps_n() >= 2) {
      char csl[16];
      format_f(csl, sizeof(csl), gcounter.get_cps_live());
      n = snprintf_P(buf, sizeof(buf),
        PSTR(",\"cs_live\":%s,\"cs_n\":%u,\"cs_win_us\":%lu"),
        csl,
        (unsigned)gcounter.get_cps_n(),
        (unsigned long)gcounter.get_cps_win_us());
      if (n > 0) res.sendChunk(buf, (size_t)n);
    }
  }
  res.sendChunk("}", 1);
  res.endChunked();
}

// /hist body + /histjs. All values come from /clicks. The body is split
// around the inter-pulse-intervals section so UDP receiver builds can skip
// it - their clicks arrive batched and the histogram stays empty.
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
  res.beginChunked(200, "text/html");
  const char* fn = DeviceInfo::friendlyName();
  const char* sub = (fn && fn[0]) ? fn : DeviceInfo::hostname();
  WebPortal::sendPageHead(res, F("History"), sub);
  res.sendChunk(FPSTR(HISTORY_BODY_LIFETIME));
#if !GEIGER_IS_UDPRX(GEIGER_TYPE)
  // Inter-pulse intervals only have meaning for builds that see per-click
  // timing. UDP receivers get batched windows from the producer, so the
  // histogram stays empty - hide it rather than ship a blank graph.
  res.sendChunk(FPSTR(HISTORY_BODY_INTERPULSE));
#endif
  res.sendChunk(FPSTR(HISTORY_BODY_LAST24));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hHistJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", "public, max-age=31536000, immutable");
#if EG_GZ_HIST_JS
  res.sendGzipP(200, "application/javascript", HIST_JS_GZ, HIST_JS_GZ_LEN);
#else
  res.send(200, "application/javascript", FPSTR(HIST_JS));
#endif
}

static void hLifeReset(EGHttpRequest& req, EGHttpResponse& res, void*) {
  gcounter.reset_lifetime();
  res.send(200, "text/plain", "OK");
}

// /pause[?s=N] - pause external posts. ?s=0 resumes. Capped at 24 h.
static void hPause(EGHttpRequest& req, EGHttpResponse& res, void*) {
  const char* sec = req.arg("s");
  if (sec) {
    int n = atoi(sec);
    if (n < 0) n = 0;
    if (n > 86400) n = 86400;
    Counter::pause_external((uint32_t)n * 1000U);
  }
  char body[40];
  uint32_t rem = Counter::pause_remaining_ms();
  snprintf_P(body, sizeof(body),
             rem ? PSTR("PAUSED %u s") : PSTR("RESUMED"), (unsigned)(rem / 1000U));
  res.send(200, "text/plain", body);
}

void CounterRoutes::registerRoutes(EGHttpServer& http) {
  http.on("/clicks",   EGHttpRequest::GET,  hClicks);
  http.on("/lastdata", EGHttpRequest::GET,  hLastData);
  http.on("/json",     EGHttpRequest::GET,  hJson);
  http.on("/hist",     EGHttpRequest::GET,  hHistory);
  http.on("/histjs",   EGHttpRequest::GET,  hHistJs);
  http.on("/life/reset", EGHttpRequest::POST, hLifeReset);
  http.on("/pause",      EGHttpRequest::GET,  hPause);
#if GEIGER_IS_TEST(GEIGER_TYPE)
  http.on("/cpm",      EGHttpRequest::GET,  hSetCPM);
#endif
}
