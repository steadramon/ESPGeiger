/*
  HV.cpp - HV generator and feedback class

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
#include "../Util/Globals.h"  // resolves ESPGEIGER_HW -> ESPG_HV before the gate
#ifdef ESPG_HV

#include <Arduino.h>
#include <EGHttpServer.h>
#include "HV.h"
#include "../Logger/Logger.h"
#include "../Util/DeviceInfo.h"
#include "../Module/EGModuleRegistry.h"
#include "../Counter/Counter.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/MathUtil.h"
#include "../Util/PinSafety.h"
#include "../Util/StringUtil.h"
#include "../WebPortal/WebPortal.h"
#include "HVJS_BUNDLE.gz.h"

extern Counter gcounter;

#define _STR(x) #x
#define STR(x)  _STR(x)

HV hv;
EG_REGISTER_MODULE(hv)

EG_PSTR(HW_L_FRQ, "PWM Frequency");
EG_PSTR(HW_H_FRQ, "HV generator frequency (Hz)");
EG_PSTR(HW_L_DTY, "PWM Duty");
EG_PSTR(HW_H_DTY, "HV generator duty (1-1023)");
#ifdef ESPG_HV_ADC
EG_PSTR(HW_L_RAT, "ADC VD Ratio");
EG_PSTR(HW_H_RAT, "Voltage divider ratio (0 = ADC disabled)");
EG_PSTR(HW_L_OFF, "ADC VD Offset");
EG_PSTR(HW_H_OFF, "Voltage divider offset");
EG_PSTR(HW_L_TGT, "HV target");
EG_PSTR(HW_H_TGT, "Auto-trim duty toward this target voltage (0 = open loop, ±5 LSB max correction)");
#endif

static const EGPref HW_PREF_ITEMS[] = {
  {"freq",   HW_L_FRQ, HW_H_FRQ, STR(GEIGERHW_FREQ), nullptr, GEIGERHW_MIN_FREQ, GEIGERHW_MAX_FREQ, 0, EGP_UINT, 0},
  {"duty",   HW_L_DTY, HW_H_DTY, STR(GEIGERHW_DUTY), nullptr, 1, 1023, 0, EGP_UINT, 0},
#ifdef ESPG_HV_ADC
  {"ratio",  HW_L_RAT, HW_H_RAT, STR(GEIGERHW_ADC_RATIO),  nullptr, 0,    50000, 0, EGP_INT, 0},
  {"offset", HW_L_OFF, HW_H_OFF, STR(GEIGERHW_ADC_OFFSET), nullptr, -100, 100,   0, EGP_INT, 0},
  {"target", HW_L_TGT, HW_H_TGT, "0", nullptr, 0, GEIGERHW_MAX_V, 0, EGP_UINT, 0},
#endif
};

static const EGPrefGroup HW_PREF_GROUP = {
  "espghw", "HV Generator", 1,
  HW_PREF_ITEMS,
  sizeof(HW_PREF_ITEMS) / sizeof(HW_PREF_ITEMS[0]),
};

const EGPrefGroup* HV::prefs_group() { return &HW_PREF_GROUP; }

void HV::on_prefs_loaded() {
  set_freq((int)EGPrefs::getUInt("espghw", "freq"));
  set_duty((int)EGPrefs::getUInt("espghw", "duty"));
#ifdef ESPG_HV_ADC
  set_vd_ratio((int)EGPrefs::getInt("espghw", "ratio"));
  set_vd_offset((int)EGPrefs::getInt("espghw", "offset"));
  set_hv_target((uint16_t)EGPrefs::getUInt("espghw", "target"));
  Log::console(PSTR("HV: freq: %d duty: %d vd: %d target: %d"), _hw_freq, _hw_duty, _hw_vd_ratio, _hv_target);
#else
  Log::console(PSTR("HV: freq: %d duty: %d"), _hw_freq, _hw_duty);
#endif
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias HW_LEGACY[] = {
  {"freq",   "freq"},
  {"duty",   "duty"},
#ifdef ESPG_HV_ADC
  {"ratio",  "ratio"},
  {"offset", "offset"},
#endif
  {nullptr, nullptr},
};
const EGLegacyAlias* HV::legacy_aliases() { return HW_LEGACY; }
// === END LEGACY IMPORT ===

HV::HV() {
}

void HV::set_duty(int duty) {
  _hw_duty = clamp(duty, 1, 1023);
}

void HV::set_pwm_pin(int pin) {
  const char* why = nullptr;
#ifdef ESP8266
  if (pin == 16) why = PSTR("no PWM on GPIO 16");
#endif
  if (!why) why = PinSafety::claim_output(pin, PSTR("HV"));
  if (why) {
    Log::console(PSTR("HV: pwm_pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  _pwm_pin = pin;
}

void HV::begin() {
  if (_pwm_pin < 0) {
    Log::console(PSTR("HV: PWM pin disabled (-1) - HV inactive"));
    EGModuleRegistry::set_loop_interval(this, 60000);
  } else {
    Log::console(PSTR("HV: PWM Setup on pin %d"), _pwm_pin);
#ifdef ESP8266
    pinMode(_pwm_pin, OUTPUT);
    analogWriteRange(1023);
    analogWrite (_pwm_pin, 0) ;
    analogWriteFreq(_hw_freq);
#else
    ledcSetup(0, _hw_freq, 10);
    ledcAttachPin(_pwm_pin, 0);
#endif
  }
#ifdef ESPG_HV_ADC
  hvReading.begin();  // default = MaxN = 20
#endif
}

void HV::loop(unsigned long /*now*/) {
  // Runs every loop_interval_ms=1000. analogRead() on ESP8266 briefly yields
  // to the WiFi stack (shared RF ADC) and can block ~150-200us - kept out of
  // the 1Hz tick so that cost doesn't inflate tick_us.
  if (_pwm_pin >= 0) {
    int eff_duty = clamp(_hw_duty + _duty_trim, 1, 1023);
    if (_cur_duty != eff_duty) {
#ifdef ESP8266
      analogWrite (_pwm_pin, eff_duty) ;
#else
      ledcWrite(0, eff_duty);
#endif
      _cur_duty = eff_duty;
    }
  }
#ifdef ESPG_HV_ADC
  if (_hw_vd_ratio == 0) return;
  int sensorValue = analogRead(GEIGER_VFEEDBACKPIN);
  int volts = (int)(((int64_t)_hw_vd_ratio * sensorValue) >> 10) + _hw_vd_offset;
  hvReading.add((float)volts);
  addFast((float)volts);

  // Closed-loop trim - only after warmup, throttled, bounded ±TRIM_MAX.
  // Suppressed for TRIM_SETTLE_MS after duty/freq change so the dip from
  // apply_freq_duty_safe doesn't trick the loop into chasing transients.
  if (_hv_target > 0 && gcounter.is_warm() && (long)(millis() - _trim_settle_until) >= 0) {
    static uint8_t trim_cnt = 0;
    if (++trim_cnt >= TRIM_PERIOD_S) {
      trim_cnt = 0;
      int err = (int)_hv_target - (int)hvReading.get();
      int8_t old_trim = _duty_trim;
      if      (err >  TRIM_HYST_V && _duty_trim <  TRIM_MAX) _duty_trim++;
      else if (err < -TRIM_HYST_V && _duty_trim > -TRIM_MAX) _duty_trim--;
      if (_duty_trim != old_trim) {
        Log::console(PSTR("HV: trim %d -> %d (err %d)"), old_trim, _duty_trim, err);
      }
    }
  }
#endif
}

void HV::apply_freq_duty_safe(int new_freq, int new_duty) {
  if (_pwm_pin >= 0) {
#ifdef ESP8266
    analogWrite(_pwm_pin, 1);
#else
    ledcWrite(0, 1);
#endif
    delay(20);
  }
  set_freq(new_freq);
  set_duty(new_duty);
  _cur_duty = -1;
  _duty_trim = 0;
  _trim_settle_until = millis() + TRIM_SETTLE_MS;
}

void HV::saveconfig() {
  char buf[16];
  snprintf_P(buf, sizeof(buf), PSTR("%d"), _hw_freq);
  EGPrefs::put("espghw", "freq", buf);
  snprintf_P(buf, sizeof(buf), PSTR("%d"), _hw_duty);
  EGPrefs::put("espghw", "duty", buf);
#ifdef ESPG_HV_ADC
  snprintf_P(buf, sizeof(buf), PSTR("%d"), _hw_vd_ratio);
  EGPrefs::put("espghw", "ratio", buf);
  snprintf_P(buf, sizeof(buf), PSTR("%d"), _hw_vd_offset);
  EGPrefs::put("espghw", "offset", buf);
  snprintf_P(buf, sizeof(buf), PSTR("%d"), _hv_target);
  EGPrefs::put("espghw", "target", buf);
#endif
  EGPrefs::commit();
}

// ---------- HTTP routes ----------

// PROGMEM blobs migrated out of ConfigManager/html.h. ConfigManager
// references them via extern decls in html.h until /hv legacy routes
// are retired.

extern const char HV_STATUS_PAGE_BODY[] PROGMEM = R"HTML(
<canvas id=g1></canvas><div id=g2></div>
<div class="a aw">
<span class="cl" onclick="this.parentElement.style.display='none';">×</span>
<strong>Disconnect your tube before adjusting! This reading is indicative only.</strong> Confirm with your HV meter.
</div>
<table><tr><th><label for=freq>Frequency:</label></th><td><input type="range" id="freq" min="0" max="9000" step="100" value="50"><span id="sfreq">-</span></td></tr>
<tr><th><label for=duty>Duty:</label></th><td><input type="range" id="duty" min="0" max="1023" value="50"><span id="sduty">-</span></td></tr>
<tr><th><label for=ratio>Ratio:</label></th><td><input id="ratio" value="-" type="number" min="0" max="9999"> <small>(voltage-divider scale, 0 = disabled)</small></td></tr>
<tr><th><label for=tgt>Target:</label></th><td><input id="tgt" value="-" type="number" min="0" max="500"> V <small>(0 = open loop) <span id="strim"></span></small></td></tr>

<tr><td><button id="submit" disabled>Loading…</button></td></tr></table>
<script src="/hvjs)HTML" EG_CACHE_BUST R"HTML("></script>
)HTML";

#if !EG_GZ_HVJS_BUNDLE
extern const char hvJS[] PROGMEM = R"JS("use strict";
var e=new Graph("g1",["Volts"],"V","g2",15,null,0,!0,!0,5,5),
D=Date,X=XMLHttpRequest,O=setTimeout,
G1=byID('g1'),G2=byID('g2'),
FQ=byID('freq'),DU=byID('duty'),RT=byID('ratio'),TG=byID('tgt'),
SF=byID('sfreq'),SD=byID('sduty'),ST=byID('strim'),SB=byID('submit'),
TRow=TG.closest('tr'),
lt,lf=0,ge=!0,inited=!1;
function showGraph(on){if(on===ge)return;ge=on;var d=on?'':'none';G1.style.display=d;G2.style.display=d;TRow.style.display=d}
function gethv(){
  clearTimeout(lt);
  var x=new X;
  x.onload=function(){
    if(x.status==200){
      var o=JSON.parse(x.responseText);
      showGraph(o.ratio>0);
      if(o.ratio>0)e.update([o.volts]);
      if(!inited){
        inited=!0;
        if(o.fmax)FQ.max=o.fmax;
        FQ.value=o.freq;DU.value=o.duty;RT.value=o.ratio;TG.value=o.target;
        SF.textContent=o.freq;SD.textContent=o.duty;
        SB.disabled=!1;SB.textContent='Submit';
      }
      ST.textContent=o.trim?'(trim '+(o.trim>0?'+':'')+o.trim+')':'';
    }
    lf=D.now();lt=O(gethv,3e3);
  };
  x.open('GET','/hvjson',!0);
  x.send();
  return!1;
}
addEventListener("load",gethv);
document.addEventListener("visibilitychange",function(){if(!document.hidden){clearTimeout(lt);lt=O(gethv,Math.max(0,3e3-(D.now()-lf)))}});
FQ.addEventListener("change",function(){SF.textContent=this.value});
DU.addEventListener("change",function(){SD.textContent=this.value});
SB.addEventListener("click",function(){
  var x=new X;
  x.onload=function(){
    if(x.status==200){
      SF.textContent='-';SD.textContent='-';inited=!1;
    }
  };
  x.open('GET','/hvset?f='+FQ.value+'&d='+DU.value+'&r='+RT.value+'&t='+TG.value,!0);
  x.send();
});
)JS";
#endif



// picographJS lives in WebPortal.cpp (shared with /js status page).
extern const char picographJS[] PROGMEM;

static void hHv(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, "text/html");
  WebPortal::sendPageHead(res, F("HV"));
  res.sendChunk(FPSTR(HV_STATUS_PAGE_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hHvSet(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // arg() returns into a shared static buffer - atoi each immediately, then
  // re-call arg() for the next field.
  int new_freq = -1, new_duty = -1, new_ratio = -1, new_target = -1;
  const char* p = req.arg("d"); if (p && *p) new_duty   = atoi(p);
  p = req.arg("f");              if (p && *p) new_freq   = atoi(p);
  p = req.arg("r");              if (p && *p) new_ratio  = atoi(p);
  p = req.arg("t");              if (p && *p) new_target = atoi(p);

  bool changed = false;
  if (new_duty >= 0 || new_freq >= 0) {
    int f = new_freq >= 0 ? new_freq : hv.get_freq();
    int d = new_duty >= 0 ? new_duty : hv.get_duty();
    hv.apply_freq_duty_safe(f, d);
    changed = true;
  }
  if (new_ratio >= 0)  { hv.set_vd_ratio(new_ratio); changed = true; }
  if (new_target >= 0) {
    hv.set_hv_target((uint16_t)clamp(new_target, 0, 500));
    changed = true;
  }
  if (changed) {
    hv.reset_trim();
    hv.saveconfig();
  }
  res.send(200, "text/plain", "OK");
}

static void hHvJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader("Cache-Control", "public, max-age=31536000, immutable");
#if EG_GZ_HVJS_BUNDLE
  res.sendGzipP(200, "application/javascript", HVJS_BUNDLE_GZ, HVJS_BUNDLE_GZ_LEN);
#else
  res.beginChunked(200, "application/javascript");
  res.sendChunk(FPSTR(picographJS));
  res.sendChunk(FPSTR(hvJS));
  res.endChunked();
#endif
}

#ifdef ESPG_HV_ADC
static void hHvJson(EGHttpRequest& req, EGHttpResponse& res, void*) {
  char buf[256];
  snprintf_P(buf, sizeof(buf),
    PSTR("{\"volts\":%d,\"freq\":%d,\"duty\":%d,\"ratio\":%d,\"target\":%d,\"trim\":%d,\"fmax\":%d}"),
    (int)hv.getFast(),
    hv.get_freq(),
    hv.get_duty(),
    hv.get_vd_ratio(),
    hv.get_hv_target(),
    hv.get_duty_trim(),
    GEIGERHW_MAX_FREQ);
  res.send(200, "application/json", buf);
}
#endif

static const EGMenuEntry HV_MENU[] = {
  {"/hv", "HV Config"},
  {nullptr, nullptr}
};
const EGMenuEntry* HV::menuEntries() { return HV_MENU; }

void HV::registerRoutes(EGHttpServer& http) {
  http.on("/hv",     EGHttpRequest::GET, hHv);
  http.on("/hvset",  EGHttpRequest::GET, hHvSet);
  http.on("/hvjs",   EGHttpRequest::GET, hHvJs);
#ifdef ESPG_HV_ADC
  http.on("/hvjson", EGHttpRequest::GET, hHvJson);
#endif
}

#endif
