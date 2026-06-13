/*
  PulseOut.cpp - Per-pulse GPIO output for piezo / speaker / LED / line-out.

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
#ifdef PULSE_OUT

#include "PulseOut.h"
#include "../Logger/Logger.h"
#include "../Module/EGModuleRegistry.h"
#include "../Prefs/EGPrefs.h"
#include "../Util/Globals.h"

#include <LittleFS.h>

#include "../Util/FastMillis.h"
#include "../Util/StringUtil.h"
#include "../NTP/NTP.h"

PulseOut pulseout;
EG_REGISTER_MODULE(pulseout)

EG_PSTR(PO_L_EN,   "Enable");
EG_PSTR(PO_H_EN,   "Per-pulse GPIO output for piezo, speaker, LED or line-out");
EG_PSTR(PO_L_PIN,  "GPIO");
EG_PSTR(PO_H_PIN,  "-1 disables. Reboot to apply.");
EG_PSTR(PO_L_MODE, "Mode");
EG_PSTR(PO_H_MODE, "0=Pulse, 1=Burst, 2=LED fade");
EG_PSTR(PO_L_PW,   "Pulse width \xc2\xb5s");
EG_PSTR(PO_H_PW,   "100-50000");
EG_PSTR(PO_L_FRQ,  "Burst Hz");
EG_PSTR(PO_H_FRQ,  "Match the piezo's marked resonance (1000-8000)");
EG_PSTR(PO_L_CYC,  "Burst cycles");
EG_PSTR(PO_H_CYC,  "1-10. More = louder and longer");
EG_PSTR(PO_L_POL,  "Polarity");
EG_PSTR(PO_H_POL,  "0=active high, 1=active low");
EG_PSTR(PO_L_FSH,  "Fade rate");
EG_PSTR(PO_H_FSH,  "0=fast, 1=medium, 2=slow");
EG_PSTR(PO_L_MHZ,  "Max Hz");
EG_PSTR(PO_H_MHZ,  "Cap clicks per second (0 = unlimited, 1-200)");
EG_PSTR(PO_L_QFR,  "Quiet from");
EG_PSTR(PO_H_QFR,  "Silence clicks from this time (blank = off)");
EG_PSTR(PO_L_QTO,  "Quiet to");
EG_PSTR(PO_H_QTO,  "End of quiet window; crosses midnight if from > to");

static const EGPref PULSE_PREF_ITEMS[] = {
  {"enable",     PO_L_EN,   PO_H_EN,   "0",    nullptr, 0,    0,     0, EGP_BOOL, 0},
  {"pin",        PO_L_PIN,  PO_H_PIN,  "-1",   nullptr, -1,   MAX_GPIO_PIN, 0, EGP_INT,  0},
  {"mode",       PO_L_MODE, PO_H_MODE, "0",    nullptr, 0,    2,     0, EGP_UINT, 0},
  {"pulse_us",   PO_L_PW,   PO_H_PW,   "5000", nullptr, 100,  50000, 0, EGP_UINT, 0},
  {"freq",       PO_L_FRQ,  PO_H_FRQ,  "3500", nullptr, 1000, 8000,  0, EGP_UINT, 0},
  {"cycles",     PO_L_CYC,  PO_H_CYC,  "1",    nullptr, 1,    10,    0, EGP_UINT, 0},
  {"polarity",   PO_L_POL,  PO_H_POL,  "0",    nullptr, 0,    1,     0, EGP_UINT, 0},
  {"fade_shift", PO_L_FSH,  PO_H_FSH,  "1",    nullptr, 0,    2,     0, EGP_UINT, 0},
  {"max_hz",     PO_L_MHZ,  PO_H_MHZ,  "20",   nullptr, 0,    200,   0, EGP_UINT, 0},
  {"quiet_from", PO_L_QFR,  PO_H_QFR,  "",     nullptr, 0,    0,     5, EGP_STRING, EGP_TIME},
  {"quiet_to",   PO_L_QTO,  PO_H_QTO,  "",     nullptr, 0,    0,     5, EGP_STRING, EGP_TIME},
};

static const EGPrefGroup PULSE_PREF_GROUP = {
  "pulse", "Pulse Out", 1,
  PULSE_PREF_ITEMS,
  sizeof(PULSE_PREF_ITEMS) / sizeof(PULSE_PREF_ITEMS[0]),
  EGP_CAT_OUTPUT,
};

const EGPrefGroup* PulseOut::prefs_group() { return &PULSE_PREF_GROUP; }

void PulseOut::on_prefs_loaded() {
  _enabled  = EGPrefs::getBool("pulse", "enable");
  int p     = EGPrefs::getInt("pulse", "pin");
  _pin      = (p < -1 || p > MAX_GPIO_PIN) ? -1 : (int8_t)p;
  _mode     = (uint8_t)EGPrefs::getUInt("pulse", "mode");
  _pulse_us = (uint16_t)EGPrefs::getUInt("pulse", "pulse_us");
  _freq_hz  = (uint16_t)EGPrefs::getUInt("pulse", "freq");
  _cycles   = (uint8_t)EGPrefs::getUInt("pulse", "cycles");
  _polarity   = (uint8_t)EGPrefs::getUInt("pulse", "polarity");
  uint8_t fade_idx = (uint8_t)EGPrefs::getUInt("pulse", "fade_shift");
  if (fade_idx > 2) fade_idx = 1;
  _fade_shift = fade_idx + 2;
  if (_mode > 2)         _mode = 0;
  if (_pulse_us < 100)   _pulse_us = 100;
  if (_pulse_us > 50000) _pulse_us = 50000;
  if (_freq_hz < 1000)   _freq_hz = 1000;
  if (_freq_hz > 8000)   _freq_hz = 8000;
  if (_cycles < 1)       _cycles = 1;
  if (_cycles > 10)      _cycles = 10;
  if (_polarity > 1)     _polarity = 0;
  _max_hz = (uint8_t)EGPrefs::getUInt("pulse", "max_hz");
  if (_max_hz > 200) _max_hz = 200;
  _token_interval_ms = _max_hz ? (uint16_t)(1000 / _max_hz) : 0;
  ParsedTime qf = parseTime(EGPrefs::getString("pulse", "quiet_from"));
  ParsedTime qt = parseTime(EGPrefs::getString("pulse", "quiet_to"));
  _q_from_min = qf.isValid ? (int16_t)(qf.hour * 60 + qf.minute) : -1;
  _q_to_min   = qt.isValid ? (int16_t)(qt.hour * 60 + qt.minute) : -1;
  recomputeEffective();
  EGModuleRegistry::set_loop_interval(this, (_enabled && _pin >= 0) ? 1 : -1);
}

bool PulseOut::isQuietNow() {
  if (_q_from_min < 0 || _q_to_min < 0) return false;
  if (_q_from_min == _q_to_min)         return false;
  // Hot path: cache result until the next minute boundary so localTm()
  // (the expensive bit) only runs once per minute instead of per click.
  static unsigned long s_recompute_ms = 0;
  static bool s_cached = false;
  unsigned long now_ms = fast_millis();
  if ((long)(now_ms - s_recompute_ms) < 0) return s_cached;
  if (!ntpclient.synced)                return false;
  struct tm t;
  if (!ntpclient.localTm(&t))           return false;
  int16_t m = t.tm_hour * 60 + t.tm_min;
  s_cached = (_q_from_min < _q_to_min)
    ? (m >= _q_from_min && m < _q_to_min)
    : (m >= _q_from_min || m < _q_to_min);
  s_recompute_ms = now_ms + (60UL - t.tm_sec) * 1000UL;
  return s_cached;
}

// === LEGACY IMPORT (remove after v1.0.0) ===
// v0.11.0 stored blip_pin + blip_pulse_ms in /prefs/led.bin (binary
// EGPrefsStorage format: 4-byte 'EGP1' magic, then klen/key/vlen/val).
// v0.12 dropped those keys from the led group and they now live in
// pulse.pin / pulse.pulse_us (with a ms->us unit change).
static const char* BLIP_MIG_MARKER = "/.pulse_blip_mig";

static bool migrate_legacy_blip() {
#ifdef ESP8266
  LittleFS.begin();
#else
  LittleFS.begin(true);
#endif
  if (LittleFS.exists(BLIP_MIG_MARKER)) return false;
  const char* path = "/prefs/led.bin";
  File f;
  if (!LittleFS.exists(path) || !(f = LittleFS.open(path, "r"))) {
    File mk = LittleFS.open(BLIP_MIG_MARKER, "w"); if (mk) mk.close();
    return false;
  }
  uint8_t magic[4];
  if (f.read(magic, 4) != 4 || memcmp(magic, "EGP1", 4) != 0) {
    f.close();
    File mk = LittleFS.open(BLIP_MIG_MARKER, "w"); if (mk) mk.close();
    return false;
  }
  char pin_val[16]  = {0};
  char ms_val[16]   = {0};
  bool any = false;
  char key[24];
  char val[64];
  while (f.available() > 0) {
    uint8_t klen;
    if (f.read(&klen, 1) != 1 || klen == 0 || klen >= sizeof(key)) break;
    if (f.read((uint8_t*)key, klen) != klen) break;
    uint8_t vb[2];
    if (f.read(vb, 2) != 2) break;
    uint16_t vlen = (uint16_t)vb[0] | ((uint16_t)vb[1] << 8);
    if (vlen >= sizeof(val)) break;
    if (vlen > 0 && f.read((uint8_t*)val, vlen) != vlen) break;
    if (klen == 8  && strncmp(key, "blip_pin", 8) == 0 && vlen < sizeof(pin_val)) {
      memcpy(pin_val, val, vlen); pin_val[vlen] = '\0';
    } else if (klen == 13 && strncmp(key, "blip_pulse_ms", 13) == 0 && vlen < sizeof(ms_val)) {
      memcpy(ms_val, val, vlen); ms_val[vlen] = '\0';
    }
  }
  f.close();
  if (pin_val[0]) {
    EGPrefs::put("pulse", "pin", pin_val);
    any = true;
  }
  if (ms_val[0]) {
    char* endp = nullptr;
    long ms = strtol(ms_val, &endp, 10);
    if (endp != ms_val && ms > 0) {
      long us = ms * 1000;
      if (us < 100)   us = 100;
      if (us > 50000) us = 50000;
      char buf[16]; snprintf(buf, sizeof(buf), "%ld", us);
      EGPrefs::put("pulse", "pulse_us", buf);
      any = true;
    }
  }
  if (any) {
    EGPrefs::put("pulse", "enable", "1");
    EGPrefs::commit(false);
    Log::console(PSTR("PulseOut: migrated blip pin=%s pulse_ms=%s from v0.11"),
                 pin_val[0] ? pin_val : "(none)", ms_val[0] ? ms_val : "(none)");
  } else {
    Log::console(PSTR("PulseOut: no blip pin/pulse keys in /prefs/led.bin"));
  }
  File mk = LittleFS.open(BLIP_MIG_MARKER, "w"); if (mk) mk.close();
  return any;
}
// === END LEGACY IMPORT ===

void PulseOut::begin() {
  if (migrate_legacy_blip()) on_prefs_loaded();
  if (!_enabled || _pin < 0) {
    if (_enabled && _pin < 0) Log::console(PSTR("PulseOut: no pin set"));
    EGModuleRegistry::set_loop_interval(this, -1);  // drop from registry walk; click path still works
    return;
  }
  pinMode(_pin, OUTPUT);
  writeIdle();
  _pin_high = false;

  // Chip-id voice jitter: +/-15% pulse width, +/-3% burst freq.
#ifdef ESP8266
  uint32_t mac = ESP.getChipId();
#else
  uint64_t m64 = ESP.getEfuseMac();
  uint32_t mac = (uint32_t)m64 ^ (uint32_t)(m64 >> 32);
#endif
  uint32_t r = mac;
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  int32_t  s = (int32_t)(r & 0xFF) - 128;
  _voice_pulse = 1.0f + s * (0.15f / 128.0f);
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  s = (int32_t)(r & 0xFF) - 128;
  _voice_freq  = 1.0f + s * (0.03f / 128.0f);
  recomputeEffective();

  Log::console(PSTR("PulseOut: GPIO%d mode=%u polarity=%u"),
               (int)_pin, (unsigned)_mode, (unsigned)_polarity);
}

// Token bucket: 1 token per 50 ms, max 5. Caps clicks at 20/s.
void PulseOut::notifyClick(unsigned long now_ms) {
  if (!_enabled || _pin < 0) return;
  if (isQuietNow()) return;
  if (_token_interval_ms) {
    while ((now_ms - _last_token_ms) >= _token_interval_ms && _tokens < 5) {
      _last_token_ms += _token_interval_ms;
      _tokens++;
    }
    if ((now_ms - _last_token_ms) > 5000) _last_token_ms = now_ms;
    if (_tokens == 0) return;
    _tokens--;
  }
  if (_phases_remaining != 0) return;
  _last_emit_ms = now_ms;
  startClick();
}

void PulseOut::startClick() {
  if (_mode == 2) {
    _brightness = 255;
    analogWrite(_pin, _polarity ? 0 : 255);
    _phases_remaining = 0;
    _next_us = (uint32_t)micros() + 5000;
    return;
  }
  if (_mode == 1) {
    uint32_t duration_us = (uint32_t)_cycles * 1000000UL / _burst_freq_eff;
    uint32_t duration_ms = (duration_us + 999u) / 1000u;
    if (duration_ms == 0) duration_ms = 1;
    tone(_pin, _burst_freq_eff, duration_ms);
    _phases_remaining = 0;
    return;
  }
  writeActive();
  _pin_high = true;
  _phases_remaining = 1;
  _period_us = _pulse_us_eff;
  _next_us = (uint32_t)micros() + _pulse_us_eff;
}

void PulseOut::recomputeEffective() {
  _pulse_us_eff   = (uint32_t)(_pulse_us * _voice_pulse);
  _burst_freq_eff = (uint32_t)(_freq_hz  * _voice_freq);
  if (_burst_freq_eff == 0) _burst_freq_eff = 1;
}

void PulseOut::loop(unsigned long /*now_ms*/) {
  // Fade mode runs an exponential decay on _brightness via one bitshift
  // per 5 ms step. Order matters: check fade first so an in-flight fade
  // doesn't collide with the pulse/burst state machine.
  if (_mode == 2 && _brightness > 0) {
    uint32_t now_us = (uint32_t)micros();
    if ((int32_t)(now_us - _next_us) < 0) return;
    _brightness -= _brightness >> _fade_shift;
    if (_brightness < 2) {
      _brightness = 0;
      analogWrite(_pin, _polarity ? 255 : 0);
      return;
    }
    analogWrite(_pin, _polarity ? (255 - _brightness) : _brightness);
    _next_us = now_us + 5000;
    return;
  }

  if (_phases_remaining == 0) return;
  uint32_t now_us = (uint32_t)micros();
  // Signed compare so wrap (every 71 min) doesn't strand a click.
  if ((int32_t)(now_us - _next_us) < 0) return;

  _pin_high = !_pin_high;
  if (_pin_high) writeActive();
  else           writeIdle();

  _phases_remaining--;
  if (_phases_remaining == 0) return;
  _next_us = now_us + _period_us;
}

#endif
