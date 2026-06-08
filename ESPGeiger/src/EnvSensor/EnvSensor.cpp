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
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/FastMillis.h"
#include "../Util/StringUtil.h"

EG_REGISTER_MODULE(envsensor)

EG_PSTR(EN_L_SDA,  "I2C SDA pin");
EG_PSTR(EN_L_SCL,  "I2C SCL pin");
EG_PSTR(EN_L_UNIT, "Temp unit");
EG_PSTR(EN_H_UNIT, "0=Celsius 1=Fahrenheit 2=Kelvin");

#define _STR(x) #x
#define STR(x) _STR(x)

static const EGPref ENV_PREF_ITEMS[] = {
  {"sda",  EN_L_SDA,  nullptr,    STR(ENV_DEFAULT_SDA), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
  {"scl",  EN_L_SCL,  nullptr,    STR(ENV_DEFAULT_SCL), nullptr, 0, MAX_GPIO_PIN, 0, EGP_UINT, 0},
  {"unit", EN_L_UNIT, EN_H_UNIT,  "0",                  nullptr, 0, 2,           0, EGP_UINT, 0},
};

static const EGPrefGroup ENV_PREF_GROUP = {
  "env", "Environment Sensor", 1,
  ENV_PREF_ITEMS,
  sizeof(ENV_PREF_ITEMS) / sizeof(ENV_PREF_ITEMS[0]),
  EGP_CAT_INPUT,
};

const EGPrefGroup* EnvSensor::prefs_group() { return &ENV_PREF_GROUP; }

void EnvSensor::readPrefs() {
  _sda  = (uint8_t)EGPrefs::getUInt("env", "sda");
  _scl  = (uint8_t)EGPrefs::getUInt("env", "scl");
  _unit = (uint8_t)EGPrefs::getUInt("env", "unit");
  if (_unit > UNIT_K) _unit = UNIT_C;
}

void EnvSensor::on_prefs_loaded() {
  readPrefs();
}

void EnvSensor::on_prefs_saved() {
  uint8_t old_sda = _sda, old_scl = _scl;
  readPrefs();
  if (old_sda != _sda || old_scl != _scl) {
    Log::console(PSTR("Env: I2C pins changed, reboot to apply"));
  }
}

void EnvSensor::begin() {
  if (_started) return;
  _started = true;
  // OLED runs Wire.begin() first; skip here or we remap SDA/SCL and freeze it.
#ifndef SSD1306_DISPLAY
  Wire.begin(_sda, _scl);
#endif
  tryDetect();
}

bool EnvSensor::tryDetect() {
  if (_drv_flags) return true;
  if (_detect_tries >= 3) return false;
  if (_bosch.begin(Wire)) _drv_flags |= DRV_BOSCH;
  if (_aht.begin(Wire))   _drv_flags |= DRV_AHT;
  if (_drv_flags == 0) {
    _detect_tries++;
    if (_detect_tries >= 3) {
      Log::debug(PSTR("Env: no sensor after %u tries, giving up"), _detect_tries);
      EGModuleRegistry::set_loop_interval(this, -1);
    }
    return false;
  }
  _st = (State*)calloc(1, sizeof(State));
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
  Log::console(PSTR("Env: %S detected"), chipName());
  return true;
}

const __FlashStringHelper* EnvSensor::chipName() const {
  if ((_drv_flags & (DRV_BOSCH | DRV_AHT)) == (DRV_BOSCH | DRV_AHT)) {
    return F("AHTxx+BMP280");   // combo module (AHT20+BMP280 sold as BME280 replacement)
  }
  if (_drv_flags & DRV_BOSCH) return _bosch.chipName();
  if (_drv_flags & DRV_AHT)   return _aht.chipName();
  if (remoteFresh())          return F("UDP");
  return nullptr;
}

void EnvSensor::loop(unsigned long /*now*/) {
  if (_drv_flags == 0) {
    if (!tryDetect()) return;
  }
  if (!_st) return;
  if (_phase == PHASE_IDLE) sampleStart();
  else                      sampleFinish();
}

void EnvSensor::sampleStart() {
  _bt = NAN; _bh = NAN; _bp = NAN;
  // Bosch is in normal/continuous mode - just fetch the latest registers.
  if (_drv_flags & DRV_BOSCH) _bosch.read(_bt, _bh, _bp);
  // AHT needs ~80 ms after trigger; defer the read to the next loop tick
  // instead of blocking with delay(80).
  if ((_drv_flags & DRV_AHT) && _aht.trigger()) {
    _phase = PHASE_WAIT_AHT;
    EGModuleRegistry::set_loop_interval(this, 80);
    return;
  }
  if (!isnan(_bt) || !isnan(_bh) || !isnan(_bp)) emaUpdate(_bt, _bh, _bp);
}

void EnvSensor::sampleFinish() {
  float at = NAN, ah = NAN, ap = NAN;
  if (_aht.readResult(at, ah, ap)) {
    // AHT temperature + humidity override Bosch readings (AHT humidity is
    // more accurate; combo boards typically pair AHT20 with BMP280 which
    // has no humidity at all).
    if (!isnan(at)) _bt = at;
    if (!isnan(ah)) _bh = ah;
  }
  if (!isnan(_bt) || !isnan(_bh) || !isnan(_bp)) emaUpdate(_bt, _bh, _bp);
  _phase = PHASE_IDLE;
  EGModuleRegistry::set_loop_interval(this, ENV_SAMPLE_MS);
}

void EnvSensor::emaUpdate(float t, float h, float p) {
  if (isnan(_st->ema_t)) {
    _st->ema_t = t;
    _st->ema_p = p;
    _st->ema_h = h;
    return;
  }
  _st->ema_t = _st->ema_t * (1.0f - ENV_EMA_ALPHA) + t * ENV_EMA_ALPHA;
  _st->ema_p = _st->ema_p * (1.0f - ENV_EMA_ALPHA) + p * ENV_EMA_ALPHA;
  if (!isnan(h)) {
    if (isnan(_st->ema_h)) _st->ema_h = h;
    else _st->ema_h = _st->ema_h * (1.0f - ENV_EMA_ALPHA) + h * ENV_EMA_ALPHA;
  }
}

float EnvSensor::tempC() const {
  if (localPresent()) return _st->ema_t;
  if (remoteFresh())  return _r_t;
  return NAN;
}

float EnvSensor::humidity() const {
  if (localPresent()) return _st->ema_h;
  if (remoteFresh())  return _r_h;
  return NAN;
}

float EnvSensor::pressure() const {
  if (localPresent()) return _st->ema_p;
  if (remoteFresh())  return _r_p;
  return NAN;
}

float EnvSensor::tempUser() const {
  float c = tempC();
  if (isnan(c)) return NAN;
  if (_unit == UNIT_F) return c * 1.8f + 32.0f;
  if (_unit == UNIT_K) return c + 273.15f;
  return c;
}

bool EnvSensor::remoteFresh() const {
  if (_r_last_ms == 0) return false;
  return (uint32_t)(fast_millis() - _r_last_ms) < ENV_REMOTE_TTL_MS;
}

void EnvSensor::setRemote(float t, float h, float p) {
  // Local hardware always wins; mirror is for headless receivers only.
  if (localPresent()) return;
  _r_t = t;
  _r_h = h;
  _r_p = p;
  _r_last_ms = fast_millis();
}

size_t EnvSensor::status_json(char* buf, size_t cap, unsigned long now) {
  if (!present()) return 0;
  (void)now;
  char b_t[12], b_h[12], b_p[12];
  format_f(b_t, sizeof(b_t), tempUser());
  format_f(b_p, sizeof(b_p), pressure());
  bool have_h = !isnan(humidity());
  if (have_h) format_f(b_h, sizeof(b_h), humidity());
  if (have_h) {
    return snprintf_P(buf, cap,
      PSTR("\"env\":{\"t\":%s,\"h\":%s,\"p\":%s,\"u\":%u,\"chip\":\"%S\"}"),
      b_t, b_h, b_p, _unit, chipName());
  }
  return snprintf_P(buf, cap,
    PSTR("\"env\":{\"t\":%s,\"h\":null,\"p\":%s,\"u\":%u,\"chip\":\"%S\"}"),
    b_t, b_p, _unit, chipName());
}

#endif  // DISABLE_ENVSENSOR
