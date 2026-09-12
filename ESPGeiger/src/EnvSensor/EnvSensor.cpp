/*
  EnvSensor.cpp - BME280 / BMP280 / AHT-family environment module.

  Copyright (C) 2026 @steadramon

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
#include "EnvSensor.h"

EnvSensor envsensor;

#ifndef DISABLE_ENVSENSOR

#include <Wire.h>
#include <math.h>
#include "../Corium/Logger/Logger.h"
#include "../Corium/Measure/EGMeasure.h"
#include "../Corium/Module/EGModuleRegistry.h"
#include "../Corium/Prefs/EGPrefs.h"
#include "../Corium/System/FastMillis.h"
#include "../Corium/System/I2CBus.h"
#include "../Corium/Util/StringUtil.h"

EG_PSTR(K_UNIT,        "unit");
EG_PSTR(K_TEMP_OFFSET, "temp_offset");
EG_PSTR(K_ALTITUDE_M,  "altitude_m");

EG_REGISTER_MODULE(envsensor)

EG_PSTR(EN_L_UNIT, "Temp unit");
EG_PSTR(EN_O_UNIT, "Celsius|Fahrenheit|Kelvin");
EG_PSTR(EN_L_TOF,  "Temp offset");
EG_PSTR(EN_H_TOF,  "0.1 C steps. -25 = -2.5 C.");
EG_PSTR(EN_L_ALT,  "Altitude (m)");
EG_PSTR(EN_H_ALT,  "Elevation in metres. Corrects pressure to sea level (0 = local station pressure).");

// pins are i2c.sda/scl
static const EGPref ENV_PREF_ITEMS[] PROGMEM = {
  {K_UNIT,        EN_L_UNIT, nullptr,    "0",                  EN_O_UNIT, 0,  2,            0, EGP_ENUM, 0},
  {K_TEMP_OFFSET, EN_L_TOF,  EN_H_TOF,   "0",                  nullptr, -100, 100,          0, EGP_INT,  EGP_ADVANCED},
  {K_ALTITUDE_M,  EN_L_ALT,  EN_H_ALT,   "0",                  nullptr, 0,    9000,         0, EGP_UINT, 0},
};

static const EGPrefGroup ENV_PREF_GROUP PROGMEM = {
  "env", "Environment Sensor", 1,
  ENV_PREF_ITEMS,
  sizeof(ENV_PREF_ITEMS) / sizeof(ENV_PREF_ITEMS[0]),
  EGP_CAT_INPUT,
};

const EGPrefGroup* EnvSensor::prefs_group() { return &ENV_PREF_GROUP; }

void EnvSensor::readPrefs() {
#ifndef SSD1306_DISPLAY   // the OLED sets them
  I2CBus::pins((uint8_t)EGPrefs::getUInt("i2c", EG_K_SDA),
               (uint8_t)EGPrefs::getUInt("i2c", EG_K_SCL));
#endif
  uint8_t unit = (uint8_t)EGPrefs::getUInt("env", K_UNIT);
  egm_set_temp_unit(unit > UNIT_K ? UNIT_C : unit);
  _temp_offset_tenths = (int8_t)EGPrefs::getInt("env", K_TEMP_OFFSET);
  _altitude_m         = (uint16_t)EGPrefs::getUInt("env", K_ALTITUDE_M);
}

void EnvSensor::on_prefs_loaded() {
  readPrefs();
}

void EnvSensor::begin() {
  if (_started) return;
  _started = true;
  if (!I2CBus::begin()) {
    EGModuleRegistry::set_loop_interval(this, -1);
    return;
  }
  tryDetect();
}

bool EnvSensor::tryDetect() {
  if (_drv_flags) return true;
  if (_detect_tries >= 3 && !_st) return false;
  if (_bosch.begin(Wire)) _drv_flags |= DRV_BOSCH;
  if (_aht.begin(Wire))   _drv_flags |= DRV_AHT;
  if (_drv_flags == 0) {
    _detect_tries++;
    if (_detect_tries >= 3) {
      if (!_st) Log::debug(PSTR("Env: no sensor after %u tries, giving up"), _detect_tries);
      EGModuleRegistry::set_loop_interval(this, _st ? ENV_RETRY_MS : -1);
    }
    return false;
  }
  // reuse State, else it leaks
  if (!_st) _st = (State*)calloc(1, sizeof(State));
  if (!_st) {
    Log::console(PSTR("Env: calloc(%u) failed"), (unsigned)sizeof(State));
    _drv_flags = 0;
    _detect_tries = 3;
    EGModuleRegistry::set_loop_interval(this, -1);
    return false;
  }
  _st->ema_t = NAN;
  _st->ema_h = NAN;
  _st->ema_p = NAN;
  _st->prev_t = NAN;
  _st->prev_h = NAN;
  _st->prev_p = NAN;
  _st->stuck = 0;
  _st->fails = 0;
  EGModuleRegistry::set_loop_interval(this, ENV_SAMPLE_MS);
  Log::console(PSTR("Env: %S detected"), chipName());
  return true;
}

const __FlashStringHelper* EnvSensor::chipName() const {
  if ((_drv_flags & (DRV_BOSCH | DRV_AHT)) == (DRV_BOSCH | DRV_AHT)) {
    return F("AHTxx+BMP280");   // sold as BME280
  }
  if (_drv_flags & DRV_BOSCH) return _bosch.chipName();
  if (_drv_flags & DRV_AHT)   return _aht.chipName();
  if (remoteFresh())          return F("UDP");
  return nullptr;
}

void EnvSensor::loop(unsigned long /*now*/) {
  if (_drv_flags == 0) {
    tryDetect();
    return;
  }
  if (!_st) return;
  if (_phase == PHASE_IDLE) sampleStart();
  else                      sampleFinish();
}

void EnvSensor::sampleStart() {
  _bt = NAN; _bh = NAN; _bp = NAN;
  _failed = false;
  if ((_drv_flags & DRV_BOSCH) && !_bosch.read(_bt, _bh, _bp)) _failed = true;
  // AHT needs ~80 ms, read next pass
  if (_drv_flags & DRV_AHT) {
    if (_aht.trigger()) {
      _phase = PHASE_WAIT_AHT;
      EGModuleRegistry::set_loop_interval(this, 80);
      return;
    }
    _failed = true;
  }
  sampleDone();
}

void EnvSensor::sampleFinish() {
  float at = NAN, ah = NAN, ap = NAN;
  if (_aht.readResult(at, ah, ap)) {
    // AHT wins, BMP280 has no humidity
    if (!isnan(at)) _bt = at;
    if (!isnan(ah)) _bh = ah;
  } else {
    _failed = true;
  }
  _phase = PHASE_IDLE;
  EGModuleRegistry::set_loop_interval(this, ENV_SAMPLE_MS);
  sampleDone();
}

// reset or gone, look again
void EnvSensor::sampleDone() {
  if (!_failed) {
    _st->fails = 0;
  } else if (++_st->fails >= ENV_FAIL_SAMPLES) {
    Log::console(PSTR("Env: %S not answering, re-detecting"), chipName());
    _drv_flags = 0;
    return;
  }
  if (!isnan(_bt) || !isnan(_bh) || !isnan(_bp)) emaUpdate(_bt, _bh, _bp);
}

static float ema(float avg, float v) {
  if (isnan(v)) return avg;
  if (isnan(avg)) return v;
  return avg * (1.0f - ENV_EMA_ALPHA) + v * ENV_EMA_ALPHA;
}

void EnvSensor::emaUpdate(float t, float h, float p) {
  // NAN breaks the run
  bool same = true, any = false;
  if (!isnan(t)) { any = true; if (t != _st->prev_t) same = false; }
  if (!isnan(h)) { any = true; if (h != _st->prev_h) same = false; }
  if (!isnan(p)) { any = true; if (p != _st->prev_p) same = false; }
  _st->prev_t = t; _st->prev_h = h; _st->prev_p = p;
  if (any && same) {
    if (++_st->stuck >= ENV_STUCK_SAMPLES) {
      Log::console(PSTR("Env: %S stuck, re-initialising"), chipName());
      _drv_flags = 0;
      return;
    }
  } else {
    _st->stuck = 0;
  }

  _st->ema_t = ema(_st->ema_t, t);
  _st->ema_h = ema(_st->ema_h, h);
  _st->ema_p = ema(_st->ema_p, p);
}

float EnvSensor::tempC() const {
  if (localPresent()) {
    float t = _st->ema_t;
    if (isnan(t)) return NAN;
    return t + (float)_temp_offset_tenths * 0.1f;
  }
  if (remoteFresh()) return _r_t;
  return NAN;
}

float EnvSensor::humidity() const {
  if (localPresent()) return _st->ema_h;
  if (remoteFresh())  return _r_h;
  return NAN;
}

float EnvSensor::pressure() const {
  if (localPresent()) {
    float p = _st->ema_p;
    if (isnan(p) || _altitude_m == 0) return p;
    // sea level, hPa, metres
    float ratio = 1.0f - (float)_altitude_m / 44330.0f;
    return p / powf(ratio, 5.255f);
  }
  if (remoteFresh()) return _r_p;
  return NAN;
}

float EnvSensor::tempUser() const {
  return egm_celsius_to(tempC(), egm_temp_unit());
}

bool EnvSensor::remoteFresh() const {
  if (_r_last_ms == 0) return false;
  return (uint32_t)(fast_millis() - _r_last_ms) < ENV_REMOTE_TTL_MS;
}

void EnvSensor::setRemote(float t, float h, float p) {
  // local wins
  if (localPresent()) return;
  _r_t = t;
  _r_h = h;
  _r_p = p;
  _r_last_ms = fast_millis();
}

static bool env(float f, EGMValue* v) {
  if (!envsensor.present() || isnan(f)) return false;
  v->f = f;
  return true;
}
static bool m_t(EGMValue* v) { return env(envsensor.tempC(), v); }
static bool m_h(EGMValue* v) { return env(envsensor.humidity(), v); }
static bool m_p(EGMValue* v) { return env(envsensor.pressure(), v); }

static bool m_tu(EGMValue* v) {
  if (!envsensor.present() || isnan(envsensor.tempC())) return false;
  v->u = egm_temp_unit();
  return true;
}

static const char MP_T[] PROGMEM = "env_temperature_celsius";
static const char MH_T[] PROGMEM = "Temperature";
static const char MP_H[] PROGMEM = "env_humidity_percent";
static const char MH_H[] PROGMEM = "Relative humidity";
static const char MP_P[] PROGMEM = "env_pressure_hpa";
static const char MH_P[] PROGMEM = "Pressure, sea-level corrected when an elevation is set";

static const EGMeasure ENV_ROWS[] PROGMEM = {
  EGM_PROW("t", "", m_t,  EGM_F32, 2, EGMU_CELSIUS, EGM_OUT_ALL,  0, MP_T, MH_T, nullptr),
  EGM_ROW("tu", "", m_tu, EGM_U32, 0, EGMU_NONE,    EGM_OUT_JSON, 0),
  EGM_PROW("h", "", m_h,  EGM_F32, 2, EGMU_PCT,     EGM_OUT_ALL,  0, MP_H, MH_H, nullptr),
  EGM_PROW("p", "", m_p,  EGM_F32, 2, EGMU_HPA,     EGM_OUT_ALL,  0, MP_P, MH_P, nullptr),
};

EGMeasureRows EnvSensor::measurements() { return EGMeasureRows(ENV_ROWS); }

#endif  // DISABLE_ENVSENSOR
