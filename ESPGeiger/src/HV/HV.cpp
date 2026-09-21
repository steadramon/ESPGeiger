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
#include "../Corium/System/Globals.h"  // before the ESPG_HV gate
#ifdef ESPG_HV

#include <Arduino.h>
#include <EGHttpServer.h>
#include "HV.h"
#include "../Corium/Logger/Logger.h"
#include "../Corium/System/DeviceInfo.h"
#include "../Corium/Module/EGModuleRegistry.h"
#include "../Counter/Counter.h"
#include "../Corium/Prefs/EGPrefs.h"
#include "../Corium/Util/MathUtil.h"
#include "../Corium/System/PinSafety.h"
#include "../Corium/Util/StringUtil.h"
#include "../Corium/Util/Words.h"
#include "../Corium/System/FastMillis.h"
#include "../Corium/System/Lifecycle.h"
#ifdef ESP8266
#ifndef EG_TIMER1
#error "ESP8266 HV runs on EGTimer1 (EG_TIMER1)"
#endif
#ifndef EG_TIMER1_PWM
#error "ESPG_HV needs EG_TIMER1_PWM"
#endif
#include <EGTimer1.h>
#endif
#include "../Corium/WebPortal/WebPortal.h"
#include "HVJS_BUNDLE.gz.h"

EG_PSTR(K_DUTY,   "duty");
EG_PSTR(K_OFFSET, "offset");
EG_PSTR(K_TARGET, "target");
#ifdef ESPG_HV_ADC
EG_PSTR(K_OV_V,   "ov_v");
EG_PSTR(K_OV_S,   "ov_s");
EG_PSTR(K_TRIM_MAX, "trim_max");
EG_PSTR(K_TRIM_S, "trim_s");
EG_PSTR(K_SETTLE_S, "settle_s");
EG_PSTR(K_BOOT_S, "boot_s");
#endif


HV hv;
EG_REGISTER_MODULE(hv)

EG_PSTR(HW_L_FRQ, "PWM Frequency");
EG_PSTR(HW_H_FRQ, "HV generator frequency (Hz)");
EG_PSTR(HW_L_DTY, "PWM Duty");
EG_PSTR(HW_H_DTY, "HV generator duty, out of " STR(HV_PWM_MAX));
#ifdef ESPG_HV_ADC
EG_PSTR(HW_L_RAT, "ADC VD Ratio");
EG_PSTR(HW_H_RAT, "Voltage divider ratio (0 = ADC disabled)");
EG_PSTR(HW_L_OFF, "ADC VD Offset");
EG_PSTR(HW_H_OFF, "Voltage divider offset");
EG_PSTR(HW_L_TGT, "HV target");
EG_PSTR(HW_H_TGT, "Auto-trim duty toward this target voltage (0 = open loop)");
#endif

static const EGPref HW_PREF_ITEMS[] PROGMEM = {
  {EG_K_FREQ,  HW_L_FRQ, HW_H_FRQ, STR(GEIGERHW_FREQ), nullptr, GEIGERHW_MIN_FREQ, GEIGERHW_MAX_FREQ, 0, EGP_UINT, EGP_ADVANCED},
  {K_DUTY,     HW_L_DTY, HW_H_DTY, STR(GEIGERHW_DUTY), nullptr, 1, GEIGERHW_MAX_DUTY, 0, EGP_UINT, EGP_ADVANCED},
#ifdef ESPG_HV_ADC
  {EG_K_RATIO, HW_L_RAT, HW_H_RAT, STR(GEIGERHW_ADC_RATIO),  nullptr, 0,    50000, 0, EGP_INT, EGP_ADVANCED},
  {K_OFFSET,   HW_L_OFF, HW_H_OFF, STR(GEIGERHW_ADC_OFFSET), nullptr, -100, 100,   0, EGP_INT, EGP_ADVANCED},
  {K_TARGET,   HW_L_TGT, HW_H_TGT, "0", nullptr, 0, GEIGERHW_MAX_V, 0, EGP_UINT, 0},
  {K_OV_V,     nullptr,  nullptr,  STR(GEIGERHW_OV_V), nullptr, 0, 2000, 0, EGP_UINT, EGP_HIDDEN},
  {K_OV_S,     nullptr,  nullptr,  STR(GEIGERHW_OV_S), nullptr, 1, 60, 0, EGP_UINT, EGP_HIDDEN},
  {K_TRIM_MAX, nullptr,  nullptr,  STR(GEIGERHW_TRIM_MAX), nullptr, 0, 20, 0, EGP_UINT, EGP_HIDDEN},
  // >= HV_TRIM_N, each step sees the last
  {K_TRIM_S,   nullptr,  nullptr,  STR(GEIGERHW_TRIM_S), nullptr, HV_TRIM_N, 120, 0, EGP_UINT, EGP_HIDDEN},
  {K_SETTLE_S, nullptr,  nullptr,  STR(GEIGERHW_SETTLE_S), nullptr, 0, 600, 0, EGP_UINT, EGP_HIDDEN},
  {K_BOOT_S,   nullptr,  nullptr,  STR(GEIGERHW_BOOT_S), nullptr, 0, 1800, 0, EGP_UINT, EGP_HIDDEN},
#endif
};

static const EGPrefGroup HW_PREF_GROUP PROGMEM = {
  "espghw", "HV Generator", 1,
  HW_PREF_ITEMS,
  sizeof(HW_PREF_ITEMS) / sizeof(HW_PREF_ITEMS[0]),
};

const EGPrefGroup* HV::prefs_group() { return &HW_PREF_GROUP; }

void HV::on_prefs_loaded() {
  set_freq((int)EGPrefs::getUInt("espghw", EG_K_FREQ));
  set_duty((int)EGPrefs::getUInt("espghw", K_DUTY));
  clear_ov();
#ifdef ESPG_HV_ADC
  set_vd_ratio((int)EGPrefs::getInt("espghw", EG_K_RATIO));
  set_vd_offset((int)EGPrefs::getInt("espghw", K_OFFSET));
  set_hv_target((uint16_t)EGPrefs::getUInt("espghw", K_TARGET));
  _ov_v = (uint16_t)EGPrefs::getUInt("espghw", K_OV_V);
  _ov_trip_s = (uint8_t)EGPrefs::getUInt("espghw", K_OV_S);
  _trim_max = (uint8_t)EGPrefs::getUInt("espghw", K_TRIM_MAX);
  _trim_s = (uint8_t)EGPrefs::getUInt("espghw", K_TRIM_S);
  _settle_s = (uint16_t)EGPrefs::getUInt("espghw", K_SETTLE_S);
  _boot_s = (uint16_t)EGPrefs::getUInt("espghw", K_BOOT_S);
  Log::console(PSTR("HV: freq: %d duty: %d vd: %d target: %d"), _hw_freq, _hw_duty, _hw_vd_ratio, _hv_target);
  if (_ov_v != GEIGERHW_OV_V) {
    Log::console(PSTR("HV: overvoltage cut-off %u V, build default %u V (0 = off)"),
                 _ov_v, (unsigned)GEIGERHW_OV_V);
  }
#else
  Log::console(PSTR("HV: freq: %d duty: %d"), _hw_freq, _hw_duty);
#endif
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias HW_LEGACY[] PROGMEM = {
  {"freq",   "freq"},
  {"duty",   "duty"},
#ifdef ESPG_HV_ADC
  {"ratio",  "ratio"},
  {"offset", "offset"},
#endif
  {},
};
EGLegacyRows HV::legacy_aliases() { return HW_LEGACY; }
// === END LEGACY IMPORT ===

HV::HV() {
}

void HV::set_duty(int duty) {
  _hw_duty = clamp(duty, 1, GEIGERHW_MAX_DUTY);
}

void HV::clear_ov() {
  if (_ov_trip) Log::console(PSTR("HV: overvoltage cut-off cleared"));
  _ov_trip = false;
  _ov_secs = 0;
  _duty_trim = 0;
  _cur_duty = -1;
}

void HV::write_duty() {
  int eff_duty = (_ov_trip || _held) ? 1 : clamp(_hw_duty + _duty_trim, 1, GEIGERHW_MAX_DUTY);
  if (_cur_duty == eff_duty) return;
#ifdef ESP8266
  EGTimer1::pwm_set((uint32_t)_hw_freq, (uint32_t)eff_duty, HV_PWM_MAX);
#else
  ledcWrite(EGLED_HV_CHANNEL, eff_duty);
#endif
  _cur_duty = eff_duty;
}

// write now, OTA stops HV::loop
void HV::hold(bool restarting) {
  _held = true;
  write_duty();
  if (!restarting) return;
#ifdef ESP8266
  EGTimer1::pwm_stop();   // leaves the pin low
#else
  ledcWrite(EGLED_HV_CHANNEL, 0);
#endif
}

void HV::release_hold() {
  _held = false;
  _cur_duty = -1;
  write_duty();
}

static void hv_lifecycle(Lifecycle::Event e) {
  if (e == Lifecycle::OTA_FAILED) hv.release_hold();
  else hv.hold(e == Lifecycle::SHUTDOWN);
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
    EGModuleRegistry::set_loop_interval(this, -1);
  } else {
    Log::console(PSTR("HV: PWM Setup on pin %d"), _pwm_pin);
#ifdef ESP8266
    pinMode(_pwm_pin, OUTPUT);
    digitalWrite(_pwm_pin, LOW);
    if (!EGTimer1::pwm_begin(_pwm_pin)) {
      Log::console(PSTR("HV: pin %d can't run the PWM - HV inactive"), (int)_pwm_pin);
      _pwm_pin = -1;
      EGModuleRegistry::set_loop_interval(this, -1);
    }
#else
    ledcSetup(EGLED_HV_CHANNEL, _hw_freq, HV_PWM_BITS);
    ledcAttachPin(_pwm_pin, EGLED_HV_CHANNEL);
#endif
    if (_pwm_pin >= 0) Lifecycle::on(hv_lifecycle);
  }
#ifdef ESPG_HV_ADC
  hvReading.begin();
  _trim_settle_until = fast_millis() + (unsigned long)_boot_s * 1000UL;
#endif
}

void HV::loop(unsigned long now) {
#ifdef ESPG_HV_ADC
  // median drops wifi ADC spikes
  if (_adc_n < HV_ADC_MAX) _adc[_adc_n++] = analogRead(GEIGER_VFEEDBACKPIN);
#endif
#ifdef ESP8266
  check_handler();
  static uint8_t pass;
  if (++pass < LOOP_SLOW) return;
  pass = 0;
#ifdef EG_TIMER1_STATS
  EGTimer1::stats_tick();
#endif
#endif
  if (_pwm_pin >= 0) write_duty();
#ifdef ESPG_HV_ADC
  if (_hw_vd_ratio == 0) return;
  bool settled = gcounter.is_warm() && (long)(now - _trim_settle_until) >= 0;
  int volts = read_hv();
  check_overvoltage(volts);
  if (_hv_target > 0 && !_ov_trip && settled) trim_step();
#endif
}

#ifdef ESP8266
// stalled handler may leave the pin high
void HV::check_handler() {
  if (_pwm_pin < 0) return;
  static uint32_t last_entries;
  static uint8_t restarts;
  uint32_t n = EGTimer1::entries();
  bool stalled = EGTimer1::pwm_running() && n == last_entries;
  last_entries = n;
  if (!stalled) return;
  EGTimer1::pwm_stop();
  if (restarts >= PWM_RESTARTS) {
    Log::console(PSTR("HV: PWM handler stopped - pin held low"));
    return;
  }
  restarts++;
  EGTimer1::restart();
  EGTimer1::pwm_begin(_pwm_pin);
  _cur_duty = -1;
  write_duty();
  Log::console(PSTR("HV: PWM handler stopped - restarted (%u of %u)"),
               (unsigned)restarts, (unsigned)PWM_RESTARTS);
}
#endif

#ifdef ESPG_HV_ADC
int HV::read_hv() {
  int volts = hv_volts(_hw_vd_ratio, hv_median2(_adc, _adc_n), 2, _hw_vd_offset);
  _adc_n = 0;
  uint16_t v = (uint16_t)(volts < 0 ? 0 : volts);
  hvReading.add(v);
  _hv_prev = v;
  return volts;
}

// raw, must trip in seconds
void HV::check_overvoltage(int volts) {
  if (_ov_trip) return;
  _ov_secs = hv_over_count(_ov_secs, volts, _ov_v);
  if (_ov_secs < _ov_trip_s) return;
  _ov_trip = true;
  if (_pwm_pin >= 0) write_duty();
  Log::console(PSTR("HV: %d V is over the %u V limit - duty held at minimum until settings are saved"),
               volts, _ov_v);
}

void HV::trim_step() {
  static uint8_t secs = 0;
  if (++secs < _trim_s) return;
  secs = 0;
  int err = (int)_hv_target - (int)hvReading.get();
  int band = hvtrim_band(_hw_vd_ratio);
  int8_t old_trim = _duty_trim;
  if      (err >  band && _duty_trim <  _trim_max) _duty_trim++;
  else if (err < -band && _duty_trim > -_trim_max) _duty_trim--;
  if (_duty_trim != old_trim) {
    Log::console(PSTR("HV: trim %d -> %d (err %d, band %d)"), old_trim, _duty_trim, err, band);
  }
}
#endif

void HV::apply_freq_duty_safe(int new_freq, int new_duty) {
  if (_pwm_pin >= 0) {
#ifdef ESP8266
    EGTimer1::pwm_set((uint32_t)_hw_freq, 1, HV_PWM_MAX);
#else
    ledcWrite(EGLED_HV_CHANNEL, 1);
#endif
    delay(20);
  }
  set_freq(new_freq);
  set_duty(new_duty);
  _cur_duty = -1;
  _duty_trim = 0;
  _trim_settle_until = fast_millis() + (unsigned long)_settle_s * 1000UL;
}

void HV::saveconfig() {
  EGPrefs::putInt("espghw", EG_K_FREQ, _hw_freq);
  EGPrefs::putInt("espghw", K_DUTY, _hw_duty);
#ifdef ESPG_HV_ADC
  EGPrefs::putInt("espghw", EG_K_RATIO, _hw_vd_ratio);
  EGPrefs::putInt("espghw", K_OFFSET, _hw_vd_offset);
  EGPrefs::putInt("espghw", K_TARGET, _hv_target);
#endif
  EGPrefs::commit();
}

extern const char HV_STATUS_PAGE_BODY[] PROGMEM = R"HTML(
<canvas id=g1></canvas><div id=g2></div>
<div class="a aw">
<span class="cl" onclick="this.parentElement.style.display='none';">×</span>
<strong>Disconnect your tube before adjusting! This reading is indicative only.</strong> Confirm with your HV meter.
</div>
<table><tr><th><label for=freq>Frequency:</label></th><td><input type="range" id="freq" min="0" max=")HTML" STR(GEIGERHW_MAX_FREQ) R"HTML(" step="100" value="50"><input id="sfreq" class=mt type="number" min="0" max=")HTML" STR(GEIGERHW_MAX_FREQ) R"HTML(" style="width:6em"> Hz</td></tr>
<tr><th><label for=duty>Duty:</label></th><td><input type="range" id="duty" min="1" max=")HTML" STR(GEIGERHW_MAX_DUTY) R"HTML(" value="50"><input id="sduty" class=mt type="number" min="1" max=")HTML" STR(GEIGERHW_MAX_DUTY) R"HTML(" style="width:6em"> <small id="pduty" data-fs=")HTML" STR(HV_PWM_MAX) R"HTML("></small></td></tr>
<tr><th><label for=ratio>Ratio:</label></th><td><input id="ratio" value="-" type="number" min="0" max="9999"> <small>(voltage-divider scale, 0 = disabled)</small></td></tr>
<tr><th><label for=tgt>Target:</label></th><td><input id="tgt" value="-" type="number" min="0" max=")HTML" STR(GEIGERHW_MAX_V) R"HTML("> V <small>(0 = open loop) <span id="strim"></span></small></td></tr>

<tr><td><button id="submit" disabled>Loading…</button></td></tr></table>
<script src="/hvjs)HTML" EG_CACHE_BUST R"HTML("></script>
)HTML";

#if !EG_GZ_HVJS_BUNDLE
extern const char hvJS[] PROGMEM = R"JS("use strict";
var e=new Graph("g1",["Volts"],"V","g2",5,null,0,!0,!0,5,15),
D=Date,X=XMLHttpRequest,O=setTimeout,
G1=byID('g1'),G2=byID('g2'),
FQ=byID('freq'),DU=byID('duty'),RT=byID('ratio'),TG=byID('tgt'),
SF=byID('sfreq'),SD=byID('sduty'),PD=byID('pduty'),ST=byID('strim'),SB=byID('submit'),
TRow=TG.closest('tr'),
lt,lf=0,ge=!0,inited=!1;
e.av=3;
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
        if(o.fmax)FQ.max=SF.max=o.fmax;
        if(o.dmax)DU.max=SD.max=o.dmax;
        FQ.value=o.freq;DU.value=o.duty;RT.value=o.ratio;TG.value=o.target;
        SF.value=o.freq;SD.value=o.duty;ton();
        SB.disabled=!1;SB.textContent='Submit';
      }
      ST.textContent=o.ov?'(overvoltage cut-off: save to retry)':o.trim?'(trim '+(o.trim>0?'+':'')+o.trim+')':'';
    }
    lf=D.now();lt=O(gethv,1e3);
  };
  x.open('GET','/hvjson',!0);
  x.send();
  return!1;
}
addEventListener("load",gethv);
document.addEventListener("visibilitychange",function(){if(!document.hidden){clearTimeout(lt);lt=O(gethv,Math.max(0,1e3-(D.now()-lf)))}});
function tie(r,n,f){
  f=f||function(){};
  r.addEventListener("input",function(){n.value=r.value;f()});
  n.addEventListener("input",function(){r.value=n.value;f()});
  n.addEventListener("change",function(){if(n.value!=='')n.value=Math.min(Math.max(Math.round(n.value),n.min),n.max);f()});
}
function ton(){PD.textContent=SD.value&&SF.value>0?'('+(SD.value*1e6/PD.dataset.fs/SF.value).toFixed(1)+' \xb5s on)':''}
tie(FQ,SF,ton);tie(DU,SD,ton);
SB.addEventListener("click",function(){
  var x=new X;
  x.onload=function(){
    if(x.status==200){
      SF.value='';SD.value='';ton();inited=!1;
    }
  };
  x.open('GET','/hvset?f='+SF.value+'&d='+SD.value+'&r='+RT.value+'&t='+TG.value,!0);
  x.send();
});
)JS";
#endif

extern const char picographJS[] PROGMEM;

static void hHv(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.beginChunked(200, FPSTR(HTTP_CT_HTML));
  WebPortal::sendPageHead(res, F("HV"));
  res.sendChunk(FPSTR(HV_STATUS_PAGE_BODY));
  WebPortal::sendPageTail(res);
  res.endChunked();
}

static void hHvSet(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // arg() buffer is shared, parse each now
  int new_freq = -1, new_duty = -1, new_ratio = -1, new_target = -1;
  int32_t v;
  const char* p = req.arg(F("d")); if (parse_i32(p, &v)) new_duty   = (int)v;
  p = req.arg(F("f"));              if (parse_i32(p, &v)) new_freq   = (int)v;
  p = req.arg(F("r"));              if (parse_i32(p, &v)) new_ratio  = (int)v;
  p = req.arg(F("t"));              if (parse_i32(p, &v)) new_target = (int)v;

  bool changed = false;
  if (new_duty >= 0 || new_freq >= 0) {
    int f = new_freq >= 0 ? new_freq : hv.get_freq();
    int d = new_duty >= 0 ? new_duty : hv.get_duty();
    hv.apply_freq_duty_safe(f, d);
    changed = true;
  }
  if (new_ratio >= 0)  { hv.set_vd_ratio(new_ratio); changed = true; }
  if (new_target >= 0) {
    hv.set_hv_target((uint16_t)clamp(new_target, 0, GEIGERHW_MAX_V));
    changed = true;
  }
  if (changed) {
    hv.clear_ov();   // also resets the trim
    hv.saveconfig();
  }
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
}

static void hHvJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if EG_GZ_HVJS_BUNDLE
  res.sendGzipP(200, FPSTR(HTTP_CT_JS), HVJS_BUNDLE_GZ, HVJS_BUNDLE_GZ_LEN);
#else
  res.beginChunked(200, FPSTR(HTTP_CT_JS));
  res.sendChunk(FPSTR(picographJS));
  res.sendChunk(FPSTR(hvJS));
  res.endChunked();
#endif
}

#ifdef ESPG_HV_ADC
static void hHvJson(EGHttpRequest& req, EGHttpResponse& res, void*) {
  int volts = hv.get_volts();
  char buf[256];
  snprintf_P(buf, sizeof(buf),
    PSTR("{\"volts\":%d,\"freq\":%d,\"duty\":%d,\"ratio\":%d,\"target\":%d,\"trim\":%d,\"fmax\":%d"
         ",\"dmax\":%d,\"ov\":%d}"),
    volts,
    hv.get_freq(),
    hv.get_duty(),
    hv.get_vd_ratio(),
    hv.get_hv_target(),
    hv.get_duty_trim(),
    GEIGERHW_MAX_FREQ,
    GEIGERHW_MAX_DUTY,
    hv.ov_tripped() ? 1 : 0);
  res.send(200, FPSTR(HTTP_CT_JSON), buf);
}

static bool m_hv(EGMValue* v) {
  if (hv.get_pwm_pin() < 0 || hv.get_vd_ratio() == 0) return false;
  v->i = (int32_t)(hv.hvReading.get() + 0.5f);
  return true;
}

static const char MP_HV[] PROGMEM = "device_hv_volts";
static const char MH_HV[] PROGMEM = "Tube high voltage";

static const EGMeasure HV_ROWS[] PROGMEM = {
  EGM_PROW("hv", "", m_hv, EGM_I32, 0, EGMU_VOLT, EGM_OUT_ALL, 0, MP_HV, MH_HV, nullptr),
};

EGMeasureRows HV::measurements() { return EGMeasureRows(HV_ROWS); }
#endif

static const EGMenuEntry HV_MENU[] PROGMEM = {
  {"/hv", "HV Config"},
  {"", ""}
};
const EGMenuEntry* HV::menuEntries() { return HV_MENU; }

void HV::registerRoutes(EGHttpServer& http) {
  http.on(F("/hv"),     EGHttpRequest::GET, hHv);
  http.on(F("/hvset"),  EGHttpRequest::GET, hHvSet);
  http.on(F("/hvjs"),   EGHttpRequest::GET, hHvJs);
#ifdef ESPG_HV_ADC
  http.on(F("/hvjson"), EGHttpRequest::GET, hHvJson);
#endif
}

#endif
