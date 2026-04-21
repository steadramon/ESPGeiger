/*
  ESPGHW.cpp - ESPGeiger hardware class
  
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
#ifdef ESPGEIGER_HW

#include <Arduino.h>
#include "ESPGHW.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"

#define _STR(x) #x
#define STR(x)  _STR(x)

ESPGeigerHW hardware;
EG_REGISTER_MODULE(hardware)

static const EGPref HW_PREF_ITEMS[] = {
  {"freq",   "PWM Frequency",  "HV generator frequency (Hz)", STR(GEIGERHW_FREQ), nullptr, GEIGERHW_MIN_FREQ, GEIGERHW_MAX_FREQ, 0, EGP_UINT, 0},
  {"duty",   "PWM Duty",       "HV generator duty (1-255)",   STR(GEIGERHW_DUTY), nullptr, 1, 255, 0, EGP_UINT, 0},
  {"ratio",  "ADC VD Ratio",   "Voltage divider ratio",       STR(GEIGERHW_ADC_RATIO),  nullptr, 0, 0, 0, EGP_INT, 0},
  {"offset", "ADC VD Offset",  "Voltage divider offset",      STR(GEIGERHW_ADC_OFFSET), nullptr, 0, 0, 0, EGP_INT, 0},
};

static const EGPrefGroup HW_PREF_GROUP = {
  "espghw", "HV Generator", 1,
  HW_PREF_ITEMS,
  sizeof(HW_PREF_ITEMS) / sizeof(HW_PREF_ITEMS[0]),
};

const EGPrefGroup* ESPGeigerHW::prefs_group() { return &HW_PREF_GROUP; }

void ESPGeigerHW::on_prefs_loaded() {
  set_freq((int)EGPrefs::getUInt("espghw", "freq"));
  set_duty((int)EGPrefs::getUInt("espghw", "duty"));
  set_vd_ratio((int)EGPrefs::getInt("espghw", "ratio"));
  set_vd_offset((int)EGPrefs::getInt("espghw", "offset"));
  Log::console(PSTR("ESPG-HW: freq: %d duty: %d vd: %d"), _hw_freq, _hw_duty, _hw_vd_ratio);
}

// === LEGACY IMPORT (remove after v1.0.0) ===
static const EGLegacyAlias HW_LEGACY[] = {
  {"freq",   "freq"},
  {"duty",   "duty"},
  {"ratio",  "ratio"},
  {"offset", "offset"},
  {nullptr, nullptr},
};
const EGLegacyAlias* ESPGeigerHW::legacy_aliases() { return HW_LEGACY; }
// === END LEGACY IMPORT ===

ESPGeigerHW::ESPGeigerHW() {
}

void ESPGeigerHW::begin() {
  Log::console(PSTR("ESPG-HW: PWM Setup"));
#ifdef ESP8266
  pinMode(GEIGER_PWMPIN, OUTPUT);
  analogWrite (GEIGER_PWMPIN, 0) ;
  analogWriteFreq(_hw_freq);
#else
  ledcSetup(0, _hw_freq, 8);
  ledcAttachPin(GEIGER_PWMPIN, 0);
#endif
  hvReading.begin(SMOOTHED_AVERAGE, 3);
}

void ESPGeigerHW::loop(unsigned long /*now*/) {
  // Runs every loop_interval_ms=1000. analogRead() on ESP8266 briefly yields
  // to the WiFi stack (shared RF ADC) and can block ~150-200us - kept out of
  // the 1Hz tick so that cost doesn't inflate tick_us.
  if (_cur_duty != _hw_duty) {
#ifdef ESP8266
    analogWrite (GEIGER_PWMPIN, _hw_duty) ;
#else
    ledcWrite(0, _hw_duty);
#endif
    _cur_duty = _hw_duty;
  }
  int sensorValue = analogRead(GEIGER_VFEEDBACKPIN);
  float volts = (_scale * sensorValue) + _hw_vd_offset;
  hvReading.add(volts);
}

void ESPGeigerHW::fiveloop() {
  int hvolts = (int)hvReading.get();
  Log::console(PSTR("ESPG-HW: HV: %d"), hvolts);
}

void ESPGeigerHW::saveconfig() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", _hw_freq);
  EGPrefs::put("espghw", "freq", buf);
  snprintf(buf, sizeof(buf), "%d", _hw_duty);
  EGPrefs::put("espghw", "duty", buf);
  snprintf(buf, sizeof(buf), "%d", _hw_vd_ratio);
  EGPrefs::put("espghw", "ratio", buf);
  snprintf(buf, sizeof(buf), "%d", _hw_vd_offset);
  EGPrefs::put("espghw", "offset", buf);
  EGPrefs::commit();
}
#endif
