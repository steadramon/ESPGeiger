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

PulseOut pulseout;
EG_REGISTER_MODULE(pulseout)

EG_PSTR(PO_L_EN,   "Enable");
EG_PSTR(PO_H_EN,   "Per-pulse output on a GPIO: piezo, speaker via MOSFET, LED, or line-out via attenuator");
EG_PSTR(PO_L_PIN,  "GPIO Pin");
EG_PSTR(PO_H_PIN,  "-1 disables. Reboot to apply.");
EG_PSTR(PO_L_MODE, "Drive mode");
EG_PSTR(PO_H_MODE, "0=single pulse, 1=resonant burst, 2=LED fade (PWM, LED only)");
EG_PSTR(PO_L_PW,   "Pulse width us");
EG_PSTR(PO_H_PW,   "Single-pulse width in microseconds (100-50000). Sub-ms for audio, several ms for LEDs.");
EG_PSTR(PO_L_FRQ,  "Burst freq Hz");
EG_PSTR(PO_H_FRQ,  "Burst frequency; for a piezo, match its marked resonance (1000-8000 Hz)");
EG_PSTR(PO_L_CYC,  "Burst cycles");
EG_PSTR(PO_H_CYC,  "Cycles per click in burst mode (1-10). More = louder + longer.");
EG_PSTR(PO_L_POL,  "Polarity");
EG_PSTR(PO_H_POL,  "0=active high (most cases), 1=active low (common-anode LEDs, inverted drivers)");
EG_PSTR(PO_L_FSH,  "Fade shift");
EG_PSTR(PO_H_FSH,  "Exponential decay rate in fade mode. 2=fast (~100ms), 3=normal (~250ms), 4=slow (~500ms).");

static const EGPref PULSE_PREF_ITEMS[] = {
  {"enable",     PO_L_EN,   PO_H_EN,   "0",    nullptr, 0,    0,     0, EGP_BOOL, 0},
  {"pin",        PO_L_PIN,  PO_H_PIN,  "-1",   nullptr, -1,   MAX_GPIO_PIN, 0, EGP_INT,  0},
  {"mode",       PO_L_MODE, PO_H_MODE, "0",    nullptr, 0,    2,     0, EGP_UINT, 0},
  {"pulse_us",   PO_L_PW,   PO_H_PW,   "500",  nullptr, 100,  50000, 0, EGP_UINT, 0},
  {"freq",       PO_L_FRQ,  PO_H_FRQ,  "3500", nullptr, 1000, 8000,  0, EGP_UINT, 0},
  {"cycles",     PO_L_CYC,  PO_H_CYC,  "3",    nullptr, 1,    10,    0, EGP_UINT, 0},
  {"polarity",   PO_L_POL,  PO_H_POL,  "0",    nullptr, 0,    1,     0, EGP_UINT, 0},
  {"fade_shift", PO_L_FSH,  PO_H_FSH,  "3",    nullptr, 2,    4,     0, EGP_UINT, 0},
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
  _fade_shift = (uint8_t)EGPrefs::getUInt("pulse", "fade_shift");
  if (_mode > 2)         _mode = 0;
  if (_pulse_us < 100)   _pulse_us = 100;
  if (_pulse_us > 50000) _pulse_us = 50000;
  if (_freq_hz < 1000)   _freq_hz = 1000;
  if (_freq_hz > 8000)   _freq_hz = 8000;
  if (_cycles < 1)       _cycles = 1;
  if (_cycles > 10)      _cycles = 10;
  if (_polarity > 1)     _polarity = 0;
  if (_fade_shift < 2)   _fade_shift = 2;
  if (_fade_shift > 4)   _fade_shift = 4;
  // Re-arm registry on enable/pin change; begin() only runs once at boot.
  EGModuleRegistry::set_loop_interval(this, (_enabled && _pin >= 0) ? 1 : -1);
}

void PulseOut::begin() {
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

  Log::console(PSTR("PulseOut: GPIO%d mode=%u polarity=%u"),
               (int)_pin, (unsigned)_mode, (unsigned)_polarity);
}

// Token bucket: 1 token per 50 ms, max 5. Caps clicks at 20/s.
void PulseOut::notifyClick(unsigned long now_ms) {
  if (!_enabled || _pin < 0) return;
  unsigned long elapsed = now_ms - _last_token_ms;
  if (elapsed >= 50) {
    uint32_t gained = elapsed / 50;
    if (_tokens + gained > 5) _tokens = 5;
    else _tokens += (uint8_t)gained;
    _last_token_ms += gained * 50;
  }
  if (_tokens == 0) return;
  if (_phases_remaining != 0) return;   // previous click still in flight
  _tokens--;
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
    // Resonant burst via tone(); bypasses the state machine.
    uint32_t freq = (uint32_t)(_freq_hz * _voice_freq);
    if (freq == 0) freq = 1;
    uint32_t duration_us = (uint32_t)_cycles * 1000000UL / freq;
    uint32_t duration_ms = (duration_us + 999u) / 1000u;
    if (duration_ms == 0) duration_ms = 1;
    tone(_pin, freq, duration_ms);
    _phases_remaining = 0;
    return;
  }
  // Mode 0: single pulse, one transition after pulse_us.
  writeActive();
  _pin_high = true;
  _phases_remaining = 1;
  _period_us = (uint32_t)(_pulse_us * _voice_pulse);
  _next_us = (uint32_t)micros() + _period_us;
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
