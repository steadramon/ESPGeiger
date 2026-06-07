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

static uint32_t default_millis() { return ::millis(); }
static EGLed::MillisFn s_clock = default_millis;

void EGLed::setClock(MillisFn fn) {
  s_clock = (fn != nullptr) ? fn : default_millis;
}

EGLed::EGLed(uint8_t pin, bool low_active)
    : _pin(pin), _low_active(low_active) {
  pinMode(_pin, OUTPUT);
  writeIdle();
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
  writeActive();
  _state   = PHASE_ON;
  _next_ms = s_clock() + on_ms;
}

void EGLed::off() {
  _state       = IDLE;
  _cycles_left = 0;
  writeIdle();
}

void EGLed::update() {
  if (_state == IDLE) return;
  uint32_t now = s_clock();
  if ((int32_t)(now - _next_ms) < 0) return;
  if (_state == PHASE_ON) {
    writeIdle();
    _state   = PHASE_OFF;
    _next_ms = now + _off_ms;
    return;
  }
  _cycles_left--;
  if (_cycles_left == 0) {
    _state = IDLE;
    return;
  }
  writeActive();
  _state   = PHASE_ON;
  _next_ms = now + _on_ms;
}

void EGLed::writeActive() {
  if (_brightness == 255) {
    digitalWrite(_pin, _low_active ? LOW : HIGH);
    return;
  }
#ifdef ESP8266
  uint16_t duty = (_brightness == 0) ? 0 : (((uint16_t)_brightness) << 2) + 3;
  if (_low_active) duty = 1023u - duty;
  analogWrite(_pin, duty);
#else
  uint8_t duty = _low_active ? (uint8_t)(255 - _brightness) : _brightness;
  analogWrite(_pin, duty);
#endif
}

void EGLed::writeIdle() {
  digitalWrite(_pin, _low_active ? HIGH : LOW);
}
