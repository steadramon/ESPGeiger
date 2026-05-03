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
#include "HV.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Counter/Counter.h"

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
EG_PSTR(HW_H_RAT, "Voltage divider ratio");
EG_PSTR(HW_L_OFF, "ADC VD Offset");
EG_PSTR(HW_H_OFF, "Voltage divider offset");
EG_PSTR(HW_L_TGT, "HV target");
EG_PSTR(HW_H_TGT, "Auto-trim duty toward this target voltage (0 = open loop, ±5 LSB max correction)");
#endif

static const EGPref HW_PREF_ITEMS[] = {
  {"freq",   HW_L_FRQ, HW_H_FRQ, STR(GEIGERHW_FREQ), nullptr, GEIGERHW_MIN_FREQ, GEIGERHW_MAX_FREQ, 0, EGP_UINT, 0},
  {"duty",   HW_L_DTY, HW_H_DTY, STR(GEIGERHW_DUTY), nullptr, 1, 1023, 0, EGP_UINT, 0},
#ifdef ESPG_HV_ADC
  {"ratio",  HW_L_RAT, HW_H_RAT, STR(GEIGERHW_ADC_RATIO),  nullptr, 0, 0, 0, EGP_INT, 0},
  {"offset", HW_L_OFF, HW_H_OFF, STR(GEIGERHW_ADC_OFFSET), nullptr, 0, 0, 0, EGP_INT, 0},
  {"target", HW_L_TGT, HW_H_TGT, "0", nullptr, 0, 500, 0, EGP_UINT, 0},
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

void HV::begin() {
  if (_pwm_pin < 0) {
    Log::console(PSTR("HV: PWM pin disabled (-1) - HV inactive"));
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
  hvReading.begin(SMOOTHED_AVERAGE, 3);
#endif
}

void HV::loop(unsigned long /*now*/) {
  // Runs every loop_interval_ms=1000. analogRead() on ESP8266 briefly yields
  // to the WiFi stack (shared RF ADC) and can block ~150-200us - kept out of
  // the 1Hz tick so that cost doesn't inflate tick_us.
  if (_pwm_pin >= 0) {
    int eff_duty = _hw_duty + _duty_trim;
    if (eff_duty < 1)    eff_duty = 1;
    if (eff_duty > 1023) eff_duty = 1023;
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
  if (GEIGER_VFEEDBACKPIN < 0) return;
  int sensorValue = analogRead(GEIGER_VFEEDBACKPIN);
  float volts = (_scale * sensorValue) + _hw_vd_offset;
  hvReading.add(volts);

  // Closed-loop trim — only after warmup, throttled, bounded ±TRIM_MAX.
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

#ifdef ESPG_HV_ADC
void HV::fiveloop() {
  int hvolts = (int)hvReading.get();
  if (_duty_trim != 0) {
    Log::console(PSTR("HV: %d trim: %d"), hvolts, _duty_trim);
  } else {
    Log::console(PSTR("HV: %d"), hvolts);
  }
}
#endif

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
#endif
