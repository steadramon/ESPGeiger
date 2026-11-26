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
#include "Corium/Logger/Logger.h"
#include "Corium/Measure/Measure.h"
#include "Corium/Module/Registry.h"
#include "Corium/Prefs/Prefs.h"
#include "Corium/System/FastMillis.h"
#include "Corium/System/I2CBus.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"
#include "App/MeasureIds.h"
#include "GeigerInput/GeigerInput.h"

// UdpRx's /env rows are the only remote source
#if GEIGER_IS_UDPRX(GEIGER_TYPE) && !defined(ENV_RX)
#define ENV_RX
#endif
#ifdef ENV_RX
static corium::MRead s_rx_t, s_rx_h, s_rx_p;
#else
static constexpr corium::MRead s_rx_t = nullptr, s_rx_h = nullptr, s_rx_p = nullptr;
#endif

CORIUM_PSTR(K_UNIT,        "unit");
CORIUM_PSTR(K_TEMP_OFFSET, "temp_offset");
CORIUM_PSTR(K_ALTITUDE_M,  "altitude_m");

CORIUM_REGISTER_MODULE(envsensor)

CORIUM_PSTR(EN_L_UNIT, "Temp unit");
CORIUM_PSTR(EN_O_UNIT, "Celsius|Fahrenheit|Kelvin");
CORIUM_PSTR(EN_L_TOF,  "Temp offset");
CORIUM_PSTR(EN_H_TOF,  "0.1 C steps, -25 = -2.5 C");
CORIUM_PSTR(EN_L_ALT,  "Altitude (m)");
CORIUM_PSTR(EN_H_ALT,  "For sea level pressure, 0 = station pressure");

// Pins come from i2c.sda/scl
static const corium::Pref ENV_PREF_ITEMS[] PROGMEM = {
  {K_UNIT,        EN_L_UNIT, nullptr,    "0",                  EN_O_UNIT, 0,  2,            0, corium::P_ENUM, 0},
  {K_TEMP_OFFSET, EN_L_TOF,  EN_H_TOF,   "0",                  nullptr, -100, 100,          0, corium::P_INT,  corium::P_ADVANCED},
  {K_ALTITUDE_M,  EN_L_ALT,  EN_H_ALT,   "0",                  nullptr, 0,    9000,         0, corium::P_UINT, 0},
};

static const corium::PrefGroup ENV_PREF_GROUP PROGMEM = {
  "env", "Environment Sensor", 1,
  ENV_PREF_ITEMS,
  sizeof(ENV_PREF_ITEMS) / sizeof(ENV_PREF_ITEMS[0]),
  corium::P_CAT_INPUT,
};

const corium::PrefGroup* EnvSensor::prefsGroup() { return &ENV_PREF_GROUP; }

void EnvSensor::readPrefs() {
#ifndef SSD1306_DISPLAY   // the OLED sets them
  corium::I2CBus::pins((uint8_t)corium::Prefs::getUInt("i2c", corium::K_SDA),
                       (uint8_t)corium::Prefs::getUInt("i2c", corium::K_SCL));
#endif
  uint8_t unit = (uint8_t)corium::Prefs::getUInt("env", K_UNIT);
  corium::m_set_temp_unit(unit > UNIT_K ? UNIT_C : unit);
  _temp_x10 = (int8_t)corium::Prefs::getInt("env", K_TEMP_OFFSET);
  _altitude_m         = (uint16_t)corium::Prefs::getUInt("env", K_ALTITUDE_M);
}

void EnvSensor::onPrefsLoaded() {
  readPrefs();
}

void EnvSensor::begin() {
  if (_started) return;
  _started = true;
#ifdef ENV_RX
  s_rx_t = corium::m_core_read(M_RX_T, corium::M_F32);
  s_rx_h = corium::m_core_read(M_RX_H, corium::M_F32);
  s_rx_p = corium::m_core_read(M_RX_P, corium::M_F32);
#endif
  if (!corium::I2CBus::begin()) {
    corium::Registry::every(this, 0);
    return;
  }
  corium::Registry::every(this, ENV_SAMPLE_MS);
  tryDetect();
  corium::Registry::dueIn(this, 0);   // first reading at boot, not at its slot
}

bool EnvSensor::tryDetect() {
  if (_drv_flags) return true;
  if (_detect_tries >= 3 && !_st) return false;
  if (_bosch.begin(Wire)) _drv_flags |= DRV_BOSCH;
  if (_aht.begin(Wire))   _drv_flags |= DRV_AHT;
  if (_drv_flags == 0) {
    if (_detect_tries < 3) _detect_tries++;
    if (_detect_tries >= 3) {
      if (!_st) corium::Log::debug(PSTR("Env: no sensor after %u tries, giving up"), _detect_tries);
      corium::Registry::every(this, _st ? ENV_RETRY_MS : 0);
    }
    return false;
  }
  // Reuse State on a re-detect, else it leaks
  if (!_st) _st = (State*)calloc(1, sizeof(State));
  if (!_st) {
    corium::Log::console(PSTR("Env: calloc(%u) failed"), (unsigned)sizeof(State));
    _drv_flags = 0;
    _detect_tries = 3;
    corium::Registry::every(this, 0);
    return false;
  }
  *_st = State();   // calloc gives zeros, this sets the NAN defaults
  corium::Registry::every(this, ENV_SAMPLE_MS);
  corium::Log::console(PSTR("Env: %s detected"), chipName());
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
  _t = NAN; _h = NAN; _p = NAN;
  _failed = false;
  if ((_drv_flags & DRV_BOSCH) && !_bosch.read(_t, _h, _p)) _failed = true;
  // AHT needs ~80 ms, read next pass
  if (_drv_flags & DRV_AHT) {
    if (_aht.trigger()) {
      _phase = PHASE_WAIT_AHT;
      corium::Registry::dueIn(this, ENV_AHT_WAIT_MS);
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
    if (!isnan(at)) _t = at;
    if (!isnan(ah)) _h = ah;
  } else {
    _failed = true;
  }
  _phase = PHASE_IDLE;
  corium::Registry::every(this, ENV_SAMPLE_MS);
  sampleDone();
}

// A reading past the chip's range is noise off the bus
static bool junk(float& v, float lo, float hi) {
  if (isnan(v) || (v >= lo && v <= hi)) return false;
  v = NAN;
  return true;
}

// Too many bad samples means the chip was reset or is gone, so detect it again
void EnvSensor::sampleDone() {
  if (junk(_t, ENV_T_MIN, ENV_T_MAX)) _failed = true;
  if (junk(_h, ENV_H_MIN, ENV_H_MAX)) _failed = true;
  if (junk(_p, ENV_P_MIN, ENV_P_MAX)) _failed = true;
  if (!_failed) {
    _st->fails = 0;
  } else if (++_st->fails >= ENV_FAIL_SAMPLES) {
    corium::Log::console(PSTR("Env: %s not answering, re-detecting"), chipName());
    _drv_flags = 0;
    return;
  }
  if (!isnan(_t) || !isnan(_h) || !isnan(_p)) emaUpdate(_t, _h, _p);
}

void EnvSensor::emaUpdate(float t, float h, float p) {
  // Stuck check, a NAN breaks the run
  bool same = true, any = false;
  if (!isnan(t)) { any = true; if (t != _st->prev_t) same = false; }
  if (!isnan(h)) { any = true; if (h != _st->prev_h) same = false; }
  if (!isnan(p)) { any = true; if (p != _st->prev_p) same = false; }
  _st->prev_t = t; _st->prev_h = h; _st->prev_p = p;
  if (any && same) {
    if (++_st->stuck >= ENV_STUCK_SAMPLES) {
      corium::Log::console(PSTR("Env: %s stuck, re-initialising"), chipName());
      _drv_flags = 0;
      return;
    }
  } else {
    _st->stuck = 0;
  }

  _st->ema_t = emaNan(_st->ema_t, t, ENV_EMA_ALPHA);
  _st->ema_h = emaNan(_st->ema_h, h, ENV_EMA_ALPHA);
  _st->ema_p = emaNan(_st->ema_p, p, ENV_EMA_ALPHA);
}

// NAN once the remote value is stale
static float rx(corium::MRead r) {
  corium::MValue v;
  return r && r(&v) ? v.f : NAN;
}

float EnvSensor::tempC() const {
  if (localPresent()) {
    float t = _st->ema_t;
    if (isnan(t)) return NAN;
    return t + (float)_temp_x10 * 0.1f;
  }
  return rx(s_rx_t);
}

float EnvSensor::humidity() const {
  if (localPresent()) return _st->ema_h;
  return rx(s_rx_h);
}

float EnvSensor::pressure() const {
  if (localPresent()) {
    float p = _st->ema_p;
    if (isnan(p) || _altitude_m == 0) return p;
    // sea level, hPa, metres
    float ratio = 1.0f - (float)_altitude_m / 44330.0f;
    return p / powf(ratio, 5.255f);
  }
  return rx(s_rx_p);
}

bool EnvSensor::remoteFresh() const {
  corium::MValue v;
  return s_rx_t && s_rx_t(&v);
}

static bool env(float f, corium::MValue* v) {
  if (!envsensor.present() || isnan(f)) return false;
  v->f = f;
  return true;
}
static bool m_t(corium::MValue* v) { return env(envsensor.tempC(), v); }
static bool m_h(corium::MValue* v) { return env(envsensor.humidity(), v); }
static bool m_p(corium::MValue* v) { return env(envsensor.pressure(), v); }

static bool m_tu(corium::MValue* v) {
  if (!envsensor.present() || isnan(envsensor.tempC())) return false;
  v->u = corium::m_temp_unit();
  return true;
}

static const char MP_T[] PROGMEM = "env_temperature_celsius";
static const char MH_T[] PROGMEM = "Temperature";
static const char MP_H[] PROGMEM = "env_humidity_percent";
static const char MH_H[] PROGMEM = "Relative humidity";
static const char MP_P[] PROGMEM = "env_pressure_hpa";
static const char MH_P[] PROGMEM = "Pressure, sea-level corrected when an elevation is set";

static const corium::MProm ENV_PROM[] PROGMEM = {
  {MP_T, nullptr, MH_T},
  {MP_H, nullptr, MH_H},
  {MP_P, nullptr, MH_P},
};
static const corium::MRow ENV_ROWS[] PROGMEM = {
  CORIUM_M_PROW("t", "", m_t,  corium::M_F32, 2, corium::MU_CELSIUS, corium::M_OUT_ALL,  0, 1),
  CORIUM_M_ROW("tu", "", m_tu, corium::M_U32, 0, corium::MU_NONE,    corium::M_OUT_JSON, 0),
  CORIUM_M_PROW("h", "", m_h,  corium::M_F32, 2, corium::MU_PCT,     corium::M_OUT_ALL,  0, 2),
  CORIUM_M_PROW("p", "", m_p,  corium::M_F32, 2, corium::MU_HPA,     corium::M_OUT_ALL,  0, 3),
};

corium::MeasureRows EnvSensor::measurements() { return corium::MeasureRows(ENV_ROWS, ENV_PROM); }

#endif  // DISABLE_ENVSENSOR
