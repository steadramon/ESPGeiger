/*
  EGLed.cpp - see header.

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
#include "EGLed.h"

#ifdef ESP32
#include <driver/ledc.h>
#endif

static uint32_t default_millis() { return ::millis(); }
static EGLed::MillisFn s_clock = default_millis;

void EGLed::setClock(MillisFn fn) {
  s_clock = (fn != nullptr) ? fn : default_millis;
}

// ---------- ESP32 LEDC bookkeeping ----------
#ifdef ESP32

namespace {
  // Shared LEDC timer at 5 kHz; instances claim channels monotonically.
  constexpr uint8_t kChannels  = 8;
  constexpr int     kLedFreqHz = 5000;

  uint8_t s_next_chan   = 0;
  bool    s_timer_ready = false;

  void ensure_timer() {
    if (s_timer_ready) return;
    ledc_timer_config_t cfg = {};
    cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
    cfg.duty_resolution = LEDC_TIMER_8_BIT;
    cfg.timer_num       = LEDC_TIMER_0;
    cfg.freq_hz         = kLedFreqHz;
    cfg.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&cfg);
    s_timer_ready = true;
  }
}  // namespace

#endif  // ESP32

// ---------- EGLed ----------

EGLed::EGLed(uint8_t pin, bool low_active)
    : _pin(pin), _low_active(low_active) {
#ifdef ESP32
  ensure_timer();
  // Wraps at kChannels; collisions are visible, not silent.
  _chan = (uint8_t)(s_next_chan++ % kChannels);
  ledc_channel_config_t cfg = {};
  cfg.gpio_num   = _pin;
  cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  cfg.channel    = (ledc_channel_t)_chan;
  cfg.intr_type  = LEDC_INTR_DISABLE;
  cfg.timer_sel  = LEDC_TIMER_0;
  cfg.duty       = 0;
  cfg.hpoint     = 0;
  ledc_channel_config(&cfg);
#else
  pinMode(_pin, OUTPUT);
#endif
  writeDuty(0);
}

void EGLed::writeDuty(uint8_t duty) {
  const uint8_t out = _low_active ? (uint8_t)(255 - duty) : duty;
#ifdef ESP8266
  // Scale 8-bit -> arduino-esp8266's default 10-bit range. The +3 nudge
  // brings the "barely on" end closer to a visible glow.
  uint16_t d;
  if (out == 0)        d = 0;
  else if (out == 255) d = 1023;
  else                 d = ((uint16_t)out << 2) + 3;
  analogWrite(_pin, d);
#elif defined(ESP32)
  // At 8-bit LEDC resolution the duty register holds 0..255 over a 256-tick
  // period. duty=255 gives 99.6% DC (one low tick remains). Bumping to 256
  // gives true 100% DC.
  const uint32_t d = (out == 255) ? 256u : out;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_chan, d);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_chan);
#else
  analogWrite(_pin, out);
#endif
}

void EGLed::blink(uint16_t on_ms, uint16_t off_ms) {
  blinkN(on_ms, off_ms, 1);
}

void EGLed::pulse(uint16_t on_ms) {
  blinkN(on_ms, 1, 1);
}

void EGLed::blinkN(uint16_t on_ms, uint16_t off_ms, uint8_t count) {
  if (count == 0) { off(); return; }
  _on_ms       = on_ms;
  _off_ms      = off_ms;
  _cycles_left = count;
  writeDuty(_brightness);
  _state   = PHASE_ON;
  _next_ms = s_clock() + on_ms;
}

void EGLed::fade(uint8_t shift) {
  _fade_shift = shift > 7 ? 7 : shift;
  _fade_value = _brightness;
  writeDuty(_fade_value);
  _state   = PHASE_FADE;
  _next_ms = s_clock() + 5;
}

void EGLed::off() {
  _state       = IDLE;
  _cycles_left = 0;
  _fade_value  = 0;
  writeDuty(0);
}

void EGLed::update() {
  if (_state == IDLE) return;
  uint32_t now = s_clock();
  if ((int32_t)(now - _next_ms) < 0) return;
  if (_state == PHASE_FADE) {
    uint8_t delta = _fade_value >> _fade_shift;
    if (delta == 0) delta = 1;
    if (_fade_value <= delta + 1) {
      _fade_value = 0;
      writeDuty(0);
      _state = IDLE;
      return;
    }
    _fade_value = (uint8_t)(_fade_value - delta);
    writeDuty(_fade_value);
    _next_ms = now + 5;
    return;
  }
  if (_state == PHASE_ON) {
    writeDuty(0);
    _state   = PHASE_OFF;
    _next_ms = now + _off_ms;
    return;
  }
  _cycles_left--;
  if (_cycles_left == 0) {
    _state = IDLE;
    return;
  }
  writeDuty(_brightness);
  _state   = PHASE_ON;
  _next_ms = now + _on_ms;
}
