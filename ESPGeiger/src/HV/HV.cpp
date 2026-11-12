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
#ifdef ESPG_HV

#include <EGHttpServer.h>
#include "HV.h"
#include "App/AppKeys.h"
#include "App/FeatureBits.h"
#include "App/MeasureIds.h"
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Module/Registry.h"
#include "Corium/Measure/Measure.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/System/PinSafety.h"
#include "Corium/Util/StringUtil.h"
#include "Corium/Util/Words.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/Lifecycle.h"
#include "Corium/System/RtcMap.h"
#include "Corium/Platform/Chip.h"
#include "GeigerInput/GeigerInput.h"
#if !GEIGER_IS_PULSE(GEIGER_TYPE)
#error "ESPG_HV needs a pulse build"
#endif
#if CORIUM_HAS_TIMER1
#ifndef EG_TIMER1
#error "ESP8266 HV runs on EGTimer1 (EG_TIMER1)"
#endif
#ifndef EG_TIMER1_PWM
#error "ESPG_HV needs EG_TIMER1_PWM"
#endif
#include <EGTimer1.h>
#endif
#include "Corium/WebPortal/WebPortal.h"
#include "HVJS_BUNDLE.gz.h"

CORIUM_PSTR(K_DUTY,   "duty");
CORIUM_PSTR(K_OFFSET, "offset");
CORIUM_PSTR(K_TARGET, "target");
#ifdef ESPG_HV_ADC
CORIUM_PSTR(K_OV_V,   "ov_v");
CORIUM_PSTR(K_OV_S,   "ov_s");
CORIUM_PSTR(K_TRIM_MAX, "trim_max");
CORIUM_PSTR(K_TRIM_S, "trim_s");
CORIUM_PSTR(K_SETTLE_S, "settle_s");
CORIUM_PSTR(K_BOOT_S, "boot_s");
#endif


HV hv;
CORIUM_REGISTER_FEATURE(hv, FEAT_HV)

CORIUM_PSTR(HW_L_FRQ, "PWM Frequency");
CORIUM_PSTR(HW_H_FRQ, "HV generator frequency (Hz)");
CORIUM_PSTR(HW_L_DTY, "PWM Duty");
CORIUM_PSTR(HW_H_DTY, "HV generator duty, out of " STR(HV_PWM_MAX));
#ifdef ESPG_HV_ADC
CORIUM_PSTR(HW_L_RAT, "ADC VD Ratio");
CORIUM_PSTR(HW_H_RAT, "Voltage divider ratio (0 = ADC disabled)");
CORIUM_PSTR(HW_L_OFF, "ADC VD Offset");
CORIUM_PSTR(HW_H_OFF, "Voltage divider offset");
CORIUM_PSTR(HW_L_TGT, "HV target");
CORIUM_PSTR(HW_H_TGT, "Auto-trim duty toward this target voltage (0 = open loop)");
#endif

static const corium::Pref HW_PREF_ITEMS[] PROGMEM = {
  {EG_K_FREQ,  HW_L_FRQ, HW_H_FRQ, STR(GEIGERHW_FREQ), nullptr, GEIGERHW_MIN_FREQ, GEIGERHW_MAX_FREQ, 0, corium::P_UINT, corium::P_ADVANCED},
  {K_DUTY,     HW_L_DTY, HW_H_DTY, STR(GEIGERHW_DUTY), nullptr, 1, GEIGERHW_MAX_DUTY, 0, corium::P_UINT, corium::P_ADVANCED},
#ifdef ESPG_HV_ADC
  {EG_K_RATIO, HW_L_RAT, HW_H_RAT, STR(GEIGERHW_ADC_RATIO),  nullptr, 0,    GEIGERHW_MAX_RATIO, 0, corium::P_INT, corium::P_ADVANCED},
  {K_OFFSET,   HW_L_OFF, HW_H_OFF, STR(GEIGERHW_ADC_OFFSET), nullptr, -100, 100,   0, corium::P_INT, corium::P_ADVANCED},
  {K_TARGET,   HW_L_TGT, HW_H_TGT, "0", nullptr, 0, GEIGERHW_MAX_V, 0, corium::P_UINT, 0},
  {K_OV_V,     nullptr,  nullptr,  STR(GEIGERHW_OV_V), nullptr, 0, 2000, 0, corium::P_UINT, corium::P_HIDDEN},
  {K_OV_S,     nullptr,  nullptr,  STR(GEIGERHW_OV_S), nullptr, 1, 60, 0, corium::P_UINT, corium::P_HIDDEN},
  {K_TRIM_MAX, nullptr,  nullptr,  STR(GEIGERHW_TRIM_MAX), nullptr, 0, 20, 0, corium::P_UINT, corium::P_HIDDEN},
  // >= HV_TRIM_N, each step sees the last
  {K_TRIM_S,   nullptr,  nullptr,  STR(GEIGERHW_TRIM_S), nullptr, HV_TRIM_N, 120, 0, corium::P_UINT, corium::P_HIDDEN},
  {K_SETTLE_S, nullptr,  nullptr,  STR(GEIGERHW_SETTLE_S), nullptr, 0, 600, 0, corium::P_UINT, corium::P_HIDDEN},
  {K_BOOT_S,   nullptr,  nullptr,  STR(GEIGERHW_BOOT_S), nullptr, 0, 1800, 0, corium::P_UINT, corium::P_HIDDEN},
#endif
};

static const corium::PrefGroup HW_PREF_GROUP PROGMEM = {
  "espghw", "HV Generator", 1,
  HW_PREF_ITEMS,
  sizeof(HW_PREF_ITEMS) / sizeof(HW_PREF_ITEMS[0]),
};

const corium::PrefGroup* HV::prefsGroup() { return &HW_PREF_GROUP; }

void HV::onPrefsLoaded() {
  set_freq((int)corium::Prefs::getUInt("espghw", EG_K_FREQ));
  set_duty((int)corium::Prefs::getUInt("espghw", K_DUTY));
  clear_ov();
#ifdef ESPG_HV_ADC
  set_vd_ratio((int)corium::Prefs::getInt("espghw", EG_K_RATIO));
  set_vd_offset((int)corium::Prefs::getInt("espghw", K_OFFSET));
  set_hv_target((uint16_t)corium::Prefs::getUInt("espghw", K_TARGET));
  _ov_v = hv_ov_limit(corium::Prefs::getUInt("espghw", K_OV_V), GEIGERHW_OV_V);
#if GEIGERHW_OV_V > 0
#define OV_ZERO "default"
#else
#define OV_ZERO "off"
#endif
  _ov_trip_s = (uint8_t)corium::Prefs::getUInt("espghw", K_OV_S);
  _trim_max = (uint8_t)corium::Prefs::getUInt("espghw", K_TRIM_MAX);
  _trim_s = (uint8_t)corium::Prefs::getUInt("espghw", K_TRIM_S);
  _settle_s = (uint16_t)corium::Prefs::getUInt("espghw", K_SETTLE_S);
  _boot_s = (uint16_t)corium::Prefs::getUInt("espghw", K_BOOT_S);
  corium::Log::console(PSTR("HV: freq: %d duty: %d vd: %d target: %d"), _freq, _duty, _vd_ratio, _hv_target);
  if (_ov_v != GEIGERHW_OV_V) {
    corium::Log::console(PSTR("HV: overvoltage cut-off %u V, build default %u V (0 = " OV_ZERO ")"),
                 _ov_v, (unsigned)GEIGERHW_OV_V);
  }
#else
  corium::Log::console(PSTR("HV: freq: %d duty: %d"), _freq, _duty);
#endif
}

// Config or console: a new freq or duty goes as /hvset's
void HV::onPrefsSaved() {
  int f = (int)corium::Prefs::getUInt("espghw", EG_K_FREQ);
  int d = (int)corium::Prefs::getUInt("espghw", K_DUTY);
  if ((f != _freq || d != _duty) && !apply_freq_duty_safe(f, d)) return;
  onPrefsLoaded();
}

// legacy import
static const corium::LegacyAlias HW_LEGACY[] PROGMEM = {
  {"freq",   "freq"},
  {"duty",   "duty"},
#ifdef ESPG_HV_ADC
  {"ratio",  "ratio"},
  {"offset", "offset"},
#endif
  {},
};
corium::LegacyRows HV::legacyAliases() { return HW_LEGACY; }

HV::HV() {
}

bool HV::set_freq(int freq) {
  freq = clamp(freq, GEIGERHW_MIN_FREQ, GEIGERHW_MAX_FREQ);
#if CORIUM_HAS_TIMER1
  force_write();  // loop() posts freq and duty together
#else
#ifdef GEIGERHW_MAX_ON_US
  // old duty at a lower freq is a longer on-time, duty first
  int cap = hv_duty_cap(freq, GEIGERHW_MAX_ON_US);
  if (_cur_duty > cap) {
    ledcWrite(EGLED_HV_CHANNEL, cap);
    _cur_duty = cap;
  }
#endif
  // 0 = refused, the old timer runs on
  if (!ledcChangeFrequency(EGLED_HV_CHANNEL, freq, HV_PWM_BITS)) {
    pwm_off(freq);
    return false;
  }
#endif
  _freq = freq;
  return true;
}

#if !CORIUM_HAS_TIMER1
// as a failed pwm_begin
void HV::pwm_off(int freq) {
  corium::Log::console(PSTR("HV: PWM can't run %d Hz - HV inactive"), freq);
  if (_pwm_pin >= 0) {
    ledcDetachPin(_pwm_pin);
    pinMode(_pwm_pin, OUTPUT);
    digitalWrite(_pwm_pin, LOW);
  }
  _pwm_pin = -1;
  corium::Registry::every(this, 0);
}
#endif

int HV::max_duty() const {
  int top = GEIGERHW_MAX_DUTY;
#ifdef GEIGERHW_MAX_ON_US
  int cap = hv_duty_cap(_freq, GEIGERHW_MAX_ON_US);
  if (cap < top) top = cap;
#endif
#if !CORIUM_HAS_TIMER1
  // ledc writes 1023 as full on, EGTimer1 keeps its own off time
  int low = HV_PWM_MAX - 1 - hv_duty_cap(_freq, HV_MIN_OFF_US);
  if (low < top) top = low;
#endif
  return top;
}

void HV::set_duty(int duty) {
  _duty = clamp(duty, 1, max_duty());
}

void HV::clear_ov() {
  if (_ov_trip) corium::Log::console(PSTR("HV: overvoltage cut-off cleared"));
  _ov_trip = false;
  _over_s = 0;
  _duty_trim = 0;
  force_write();
}

void HV::write_duty() {
#ifdef ESPGEIGER_HW32
  if (_fb_fault) return;   // pin let go
#endif
  // tripped or held runs at duty 1
  int eff_duty = (_ov_trip || _held) ? 1 : clamp(_duty + _duty_trim, 1, max_duty());
#ifdef ESPGEIGER_HW32
  eff_duty = hv_ramp_duty(eff_duty, _ramp, GEIGERHW_RAMP_S);
#endif
  if (_cur_duty == eff_duty) return;
#if CORIUM_HAS_TIMER1
  EGTimer1::pwm_set((uint32_t)_freq, (uint32_t)eff_duty, HV_PWM_MAX);
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
#if CORIUM_HAS_TIMER1
  EGTimer1::pwm_stop();   // leaves the pin low
#else
  ledcWrite(EGLED_HV_CHANNEL, 0);
#endif
}

void HV::release_hold() {
  _held = false;
  force_write();
  write_duty();
}

static void hv_lifecycle(corium::Lifecycle::Event e) {
  if (e == corium::Lifecycle::OTA_FAILED) hv.release_hold();
  else hv.hold(e == corium::Lifecycle::SHUTDOWN);
}

void HV::set_pwm_pin(int pin) {
  const char* why = nullptr;
#ifdef ESP8266
  if (pin == 16) why = PSTR("no PWM on GPIO 16");
#endif
  if (!why) why = corium::PinSafety::claimOutput(pin, PSTR("HV"));
  if (why) {
    corium::Log::console(PSTR("HV: pwm_pin=%d unsafe (%s) - disabled"), pin, why);
    pin = -1;
  }
  _pwm_pin = pin;
}

void HV::begin() {
  if (_pwm_pin < 0) {
    corium::Log::console(PSTR("HV: PWM pin disabled (-1) - HV inactive"));
    corium::Registry::every(this, 0);
  } else {
    corium::Log::console(PSTR("HV: PWM Setup on pin %d"), _pwm_pin);
#if CORIUM_HAS_TIMER1
    pinMode(_pwm_pin, OUTPUT);
    digitalWrite(_pwm_pin, LOW);
    if (!EGTimer1::pwm_begin(_pwm_pin)) {
      corium::Log::console(PSTR("HV: pin %d can't run the PWM - HV inactive"), (int)_pwm_pin);
      _pwm_pin = -1;
      corium::Registry::every(this, 0);
    }
#else
    if (ledcSetup(EGLED_HV_CHANNEL, _freq, HV_PWM_BITS)) ledcAttachPin(_pwm_pin, EGLED_HV_CHANNEL);
    else pwm_off(_freq);
#endif
    if (_pwm_pin >= 0) {
      corium::Lifecycle::on(hv_lifecycle);
      corium::Registry::every(this, LOOP_MS);
    }
  }
#ifdef ESPG_HV_ADC
  hvReading.begin();
  _settle.set(corium::fastMillis() + (unsigned long)_boot_s * 1000UL);
  restore_trim();
#endif
}

void HV::loop(unsigned long now) {
#ifdef ESPG_HV_ADC
  sample_adc();
#endif
#if CORIUM_HAS_TIMER1
  check_handler();
#endif
#if CORIUM_HAS_TIMER1 || defined(ESPG_HV_ADC)
  if (++_pass < LOOP_SLOW) return;
  _pass = 0;
#endif
#if CORIUM_HAS_TIMER1 && defined(EG_TIMER1_STATS)
  EGTimer1::stats_tick();
#endif
#ifdef ESPGEIGER_HW32
  check_feedback();   // the sample is last second's duty, ratio 0 doesn't turn this off
  if (_ramp < GEIGERHW_RAMP_S) _ramp++;
  note_low();
#endif
  if (_pwm_pin >= 0) write_duty();
#ifdef ESPG_HV_ADC
  adc_second(now);
#endif
}

#ifdef ESPG_HV_ADC
void HV::adc_second(unsigned long now) {
  if (_settle.due(now)) _settle.clear();   // a held compare goes stale at the wrap
  if (_vd_ratio == 0) {
    _adc_n = 0;
    return;
  }
  bool settled = corium::m_b(M_WARM) && !_settle.armed();
  int volts = read_hv();
  check_overvoltage(volts);
  if (_hv_target > 0 && !_ov_trip && settled) trim_step();
}
#endif

#if CORIUM_HAS_TIMER1
// stalled handler may leave the pin high
void HV::check_handler() {
  if (_pwm_pin < 0) return;
  uint32_t n = EGTimer1::entries();
  bool stalled = EGTimer1::pwm_running() && n == _t1_entries;
  _t1_entries = n;
  if (!stalled) return;
  EGTimer1::pwm_stop();
  if (_t1_restarts >= PWM_RESTARTS) {
    corium::Log::console(PSTR("HV: PWM handler stopped - pin held low"));
    return;
  }
  _t1_restarts++;
  EGTimer1::restart();
  EGTimer1::pwm_begin(_pwm_pin);
  force_write();
  write_duty();
  corium::Log::console(PSTR("HV: PWM handler stopped - restarted (%u of %u)"),
               (unsigned)_t1_restarts, (unsigned)PWM_RESTARTS);
}
#endif

#ifdef ESPG_HV_ADC
// median drops wifi ADC spikes
void HV::sample_adc() {
  if (_adc_n >= HV_ADC_MAX) return;
#ifdef ESPGEIGER_HW32
  _adc[_adc_n++] = analogReadMilliVolts(GEIGER_VFEEDBACKPIN);   // calibrated, ratio in mV
#else
  _adc[_adc_n++] = analogRead(GEIGER_VFEEDBACKPIN);
#endif
}

int HV::read_hv() {
  int volts = hv_volts(_vd_ratio, hv_median2(_adc, _adc_n), 2, _vd_offset);
  _adc_n = 0;
  uint16_t v = (uint16_t)(volts < 0 ? 0 : volts);
  hvReading.add(v);
  _hv_prev = v;
  return volts;
}

// raw, must trip in seconds
void HV::check_overvoltage(int volts) {
  if (_ov_trip) return;
  _over_s = hv_over_count(_over_s, volts, _ov_v);
  if (_over_s < _ov_trip_s) return;
  _ov_trip = true;
  if (_pwm_pin >= 0) write_duty();
  corium::Log::console(PSTR("HV: %d V is over the %u V limit - duty held at minimum until settings are saved"),
               volts, _ov_v);
}

#ifdef ESPGEIGER_HW32
// divider open reads ~0 while driving, low leg open the ADC top
void HV::check_feedback() {
  if (_fb_fault || _pwm_pin < 0 || !_adc_n) return;
  int mv = (int)(hv_median2(_adc, _adc_n) / 2);
  _fb_bad_s = hv_bad_count(_fb_bad_s, mv, _cur_duty > 1, GEIGERHW_FB_LOW_MV, GEIGERHW_FB_HIGH_MV);
  if (_fb_bad_s < GEIGERHW_FB_BAD_S) return;
  _fb_fault = true;
  ledcWrite(EGLED_HV_CHANNEL, 0);
  ledcDetachPin(_pwm_pin);
  pinMode(_pwm_pin, INPUT);   // base pull-down holds it off
  corium::Log::console(PSTR("HV: feedback %d mV makes no sense - HV off until restart"), mv);
  corium::Registry::emit(TOPIC_FAULT);
}
#endif

static int trim_band(int ratio) {
  int band = hvtrim_band(ratio);
  return band < GEIGERHW_TRIM_BAND_MIN ? GEIGERHW_TRIM_BAND_MIN : band;
}

#ifdef ESPGEIGER_HW32
// last second's reading
void HV::note_low() {
  bool top = _hv_target > 0 && _vd_ratio > 0 && _duty_trim >= _trim_max;
  int err = (int)_hv_target - (int)hvReading.get();
  _low_s = hv_low_count(_low_s, err, trim_band(_vd_ratio), top);
}
#endif

// one duty step every trim_s, outside the band, up to +-trim_max
void HV::trim_step() {
  if (++_trim_age_s < _trim_s) return;
  _trim_age_s = 0;
  int err = (int)_hv_target - (int)hvReading.get();
  int band = trim_band(_vd_ratio);
  int8_t old_trim = _duty_trim;
  if      (err >  band && _duty_trim <  _trim_max) _duty_trim++;
  else if (err < -band && _duty_trim > -_trim_max) _duty_trim--;
  if (_duty_trim != old_trim) {
    corium::Log::console(PSTR("HV: trim %d -> %d (err %d, band %d)"), old_trim, _duty_trim, err, band);
    stash_trim();
  }
}

// the trim over a restart, not a power cut
namespace {
constexpr uint32_t TRIM_RTC_MAGIC = 0xCAFE7121;  // bump on layout change
struct TrimRtc { uint32_t magic; uint32_t freq; uint16_t duty, target, ratio; int8_t trim; uint8_t pad; };
CORIUM_RTC_SLOT(TrimSlot, TrimRtc, corium::RtcMap::APP2_AT, corium::RtcMap::APP2_WORDS);
}

void HV::stash_trim() {
  TrimSlot::write(TrimRtc{TRIM_RTC_MAGIC, (uint32_t)_freq, (uint16_t)_duty, _hv_target,
                          (uint16_t)_vd_ratio, _duty_trim, 0});
}

// learned at this duty, freq, target and ratio only
void HV::restore_trim() {
  TrimRtc r;
  if (!TrimSlot::read(r) || r.magic != TRIM_RTC_MAGIC) return;
  if (r.freq != (uint32_t)_freq || r.duty != _duty || r.target != _hv_target || r.ratio != _vd_ratio) return;
  _duty_trim = (int8_t)clamp((int)r.trim, -(int)_trim_max, (int)_trim_max);
}
#endif

bool HV::apply_freq_duty_safe(int new_freq, int new_duty) {
  if (_pwm_pin >= 0) {
#if CORIUM_HAS_TIMER1
    EGTimer1::pwm_set((uint32_t)_freq, 1, HV_PWM_MAX);
#else
    ledcWrite(EGLED_HV_CHANNEL, 1);
#endif
    delay(20);
  }
  if (!set_freq(new_freq)) return false;
  set_duty(new_duty);
  force_write();
  _duty_trim = 0;
  if (_pwm_pin >= 0) write_duty();   // not at the next second
  _settle.set(corium::fastMillis() + (unsigned long)_settle_s * 1000UL);
  return true;
}

// a refused one says not saved, the rest still go
bool HV::saveconfig() {
  bool ok = corium::Prefs::putInt("espghw", EG_K_FREQ, _freq);
  ok &= corium::Prefs::putInt("espghw", K_DUTY, _duty);
#ifdef ESPG_HV_ADC
  ok &= corium::Prefs::putInt("espghw", EG_K_RATIO, _vd_ratio);
  ok &= corium::Prefs::putInt("espghw", K_OFFSET, _vd_offset);
  ok &= corium::Prefs::putInt("espghw", K_TARGET, _hv_target);
#endif
  return corium::Prefs::commitSoon(corium::fastMillis()) && ok;
}

extern const char HV_STATUS_PAGE_BODY[] PROGMEM = R"HTML(
<canvas id=g1></canvas><div id=g2></div>
<div class="a aw">
<span class="cl" onclick="this.parentElement.style.display='none';">×</span>
<strong>Disconnect your tube before adjusting! This reading is indicative only.</strong> Confirm with your HV meter.
</div>
<table><tr><th><label for=freq>Frequency:</label></th><td><input type="range" id="freq" min=")HTML" STR(GEIGERHW_MIN_FREQ) R"HTML(" max=")HTML" STR(GEIGERHW_MAX_FREQ) R"HTML(" step="100" value="50"><input id="sfreq" class=mt type="number" min=")HTML" STR(GEIGERHW_MIN_FREQ) R"HTML(" max=")HTML" STR(GEIGERHW_MAX_FREQ) R"HTML(" style="width:6em"> Hz</td></tr>
<tr><th><label for=duty>Duty:</label></th><td><input type="range" id="duty" min="1" max=")HTML" STR(GEIGERHW_MAX_DUTY) R"HTML(" value="50"><input id="sduty" class=mt type="number" min="1" max=")HTML" STR(GEIGERHW_MAX_DUTY) R"HTML(" style="width:6em"> <small id="pduty" data-fs=")HTML" STR(HV_PWM_MAX) R"HTML("></small></td></tr>
<tr><th><label for=ratio>Ratio:</label></th><td><input id="ratio" value="-" type="number" min="0" max=")HTML" STR(GEIGERHW_MAX_RATIO) R"HTML("> <small>(voltage-divider scale, 0 = disabled)</small></td></tr>
<tr><th><label for=tgt>Target:</label></th><td><input id="tgt" value="-" type="number" min="0" max=")HTML" STR(GEIGERHW_MAX_V) R"HTML("> V <small>(0 = open loop) <span id="strim"></span></small></td></tr>

<tr><td><button id="submit" disabled>Loading…</button></td></tr></table>
)HTML"
#if !CORIUM_HAS_FEW_SOCKETS
R"HTML(<script src="/pgjs)HTML" CORIUM_CACHE_BUST R"HTML("></script>
)HTML"
#endif
R"HTML(<script src="/hvjs)HTML" CORIUM_CACHE_BUST R"HTML("></script>
)HTML";

#if !EG_GZ_HVJS_BUNDLE
extern const char hvJS[] PROGMEM = R"JS(
var e=new Graph("g1",["Volts"],"V","g2",5,null,0,!0,!0,5,15),
G1=byID('g1'),G2=byID('g2'),
FQ=byID('freq'),DU=byID('duty'),RT=byID('ratio'),TG=byID('tgt'),
SF=byID('sfreq'),SD=byID('sduty'),PD=byID('pduty'),ST=byID('strim'),SB=byID('submit'),
TRow=TG.closest('tr'),
M=['','(overvoltage cut-off: save to retry)','(feedback fault: HV off until restart)','(trim at max, still under target: raise duty or lower target)'],
ge=!0,inited=!1;
e.av=3;
function showGraph(on){if(on===ge)return;ge=on;var d=on?'':'none';G1.style.display=d;G2.style.display=d;TRow.style.display=d}
var hvK=live('/hvjson',1e3,function(s){
  if(s==null)return 1e3;
  var o=JSON.parse(s);
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
  ST.textContent=M[o.ov]||(o.trim?'(trim '+(o.trim>0?'+':'')+o.trim+')':'');
});
addEventListener("load",()=>hvK(0));
function tie(r,n,f){
  f=f||function(){};
  r.addEventListener("input",function(){n.value=r.value;f()});
  n.addEventListener("input",function(){r.value=n.value;f()});
  n.addEventListener("change",function(){if(n.value!=='')n.value=Math.min(Math.max(Math.round(n.value),n.min),n.max);f()});
}
function ton(){PD.textContent=SD.value&&SF.value>0?'('+(SD.value*1e6/PD.dataset.fs/SF.value).toFixed(1)+' \xb5s on)':''}
tie(FQ,SF,ton);tie(DU,SD,ton);
SB.addEventListener("click",function(){
  fetch('/hvset?f='+SF.value+'&d='+SD.value+'&r='+RT.value+'&t='+TG.value).then(r=>{
    if(r.ok){SF.value='';SD.value='';ton();inited=!1}
  });
});
)JS";
#endif

#if CORIUM_HAS_FEW_SOCKETS
extern const char picographJS[] PROGMEM;
#endif

static void hHv(EGHttpRequest& req, EGHttpResponse& res, void*) {
  corium::WebPortal::beginPage(res, F("HV"));
  res.sendChunk(FPSTR(HV_STATUS_PAGE_BODY));
  corium::WebPortal::endPage(res);
}

static void hHvSet(EGHttpRequest& req, EGHttpResponse& res, void*) {
  // arg() buffer is shared, parse each now
  int new_freq = -1, new_duty = -1, new_ratio = -1, new_target = -1;
  int32_t v;
  const char* p = req.arg(F("d")); if (parseI32(p, &v)) new_duty   = (int)v;
  p = req.arg(F("f"));              if (parseI32(p, &v)) new_freq   = (int)v;
  p = req.arg(F("r"));              if (parseI32(p, &v)) new_ratio  = (int)v;
  p = req.arg(F("t"));              if (parseI32(p, &v)) new_target = (int)v;

  bool changed = false;
  if (new_duty >= 0 || new_freq >= 0) {
    int f = new_freq >= 0 ? new_freq : hv.get_freq();
    int d = new_duty >= 0 ? new_duty : hv.get_duty();
    if (!hv.apply_freq_duty_safe(f, d)) {
      res.send(400, FPSTR(HTTP_CT_PLAIN), F("Not saved: the PWM can't run that frequency, HV off"));
      return;
    }
    changed = true;
  }
  if (new_ratio >= 0)  { hv.set_vd_ratio(new_ratio); changed = true; }
  if (new_target >= 0) {
    hv.set_hv_target((uint16_t)clamp(new_target, 0, GEIGERHW_MAX_V));
    changed = true;
  }
  if (changed) {
    hv.clear_ov();   // also resets the trim
    if (!hv.saveconfig()) {
      char why[CORIUM_PREFS_FROZEN_MAX];
      char buf[sizeof("Not saved: ") + CORIUM_PREFS_FROZEN_MAX];
      if (corium::Prefs::frozen(why, sizeof(why))) {
        snprintf_P(buf, sizeof(buf), PSTR("Not saved: %s"), why);
        res.send(500, FPSTR(HTTP_CT_PLAIN), buf);
      } else {
        res.send(500, FPSTR(HTTP_CT_PLAIN), F("Not saved"));
      }
      return;
    }
  }
  res.send(200, FPSTR(HTTP_CT_PLAIN), FPSTR(W_OK_TEXT));
}

static void hHvJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_HVJS_BUNDLE
  corium::WebPortal::sendGz(res, FPSTR(HTTP_CT_JS), HVJS_BUNDLE_GZ, HVJS_BUNDLE_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
#if CORIUM_HAS_FEW_SOCKETS
  res.beginChunked(200, FPSTR(HTTP_CT_JS));
  res.sendChunk(FPSTR(picographJS));
  res.sendChunk(FPSTR(hvJS));
  res.endChunked();
#else
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(hvJS));
#endif
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
    hv.max_duty(),
    hv.status());
  res.send(200, FPSTR(HTTP_CT_JSON), buf);
}

static bool m_hv(corium::MValue* v) {
  if (hv.get_pwm_pin() < 0 || hv.get_vd_ratio() == 0) return false;
  v->i = (int32_t)(hv.hvReading.get() + 0.5f);
  return true;
}

static bool m_hv_tgt(corium::MValue* v)  { v->u = hv.get_hv_target(); return true; }
static bool m_hv_duty(corium::MValue* v) { v->u = (uint32_t)hv.get_duty(); return true; }
static bool m_hv_trim(corium::MValue* v) { v->i = hv.get_duty_trim(); return true; }

static const char MP_HV[] PROGMEM = "device_hv_volts";
static const char MH_HV[] PROGMEM = "Tube high voltage";

static const corium::Measure HV_ROWS[] PROGMEM = {
  CORIUM_M_PROW("hv", "", m_hv, corium::M_I32, 0, corium::MU_VOLT, corium::M_OUT_ALL, 0, MP_HV, MH_HV, nullptr),
  // the /hv blip
  CORIUM_M_ROW("hv_tgt",  "", m_hv_tgt,  corium::M_U32, 0, corium::MU_VOLT, corium::M_OUT_ID, 0),
  CORIUM_M_ROW("hv_duty", "", m_hv_duty, corium::M_U32, 0, corium::MU_NONE, corium::M_OUT_ID, 0),
  CORIUM_M_ROW("hv_trim", "", m_hv_trim, corium::M_I32, 0, corium::MU_NONE, corium::M_OUT_ID, 0),
};

corium::MeasureRows HV::measurements() { return corium::MeasureRows(HV_ROWS); }
#endif

static const corium::MenuEntry HV_MENU[] PROGMEM = {
  {"/hv", "HV Config"},
  {"", ""}
};
const corium::MenuEntry* HV::menuEntries() { return HV_MENU; }

void HV::registerRoutes(EGHttpServer& http) {
  http.on(F("/hv"),     EGHttpRequest::GET, hHv);
  http.on(F("/hvset"),  EGHttpRequest::GET, hHvSet);
  http.on(F("/hvjs"),   EGHttpRequest::GET, hHvJs);
#ifdef ESPG_HV_ADC
  http.on(F("/hvjson"), EGHttpRequest::GET, hHvJson);
#endif
}

// console
#ifdef ESPG_HV_ADC
static void cmd_hv() { corium::Log::console(PSTR("HV: %d"), (int)hv.hvReading.get()); }
CORIUM_REGISTER_CMD(hv, cmd_hv)
#endif
#ifdef EG_HV_STALL_TEST
static void cmd_t1stop() { timer1_disable(); }
CORIUM_REGISTER_CMD(t1stop, cmd_t1stop)
#endif

#endif
