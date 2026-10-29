/*
  GeigerInput/Type/Pulse.cpp - Class for Pulse type counter

  Copyright (C) 2024 @steadramon

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
#include "Pulse.h"

#if GEIGER_IS_PULSE(GEIGER_TYPE)

#include "Corium/Logger/Logger.h"
#include "Corium/Util/MathUtil.h"
#ifdef PCNT_SELECTABLE
#include "Counter/Counter.h"
#endif

GeigerPulse::GeigerPulse() {
};

void GeigerPulse::begin() {
  GeigerInput::begin();
#ifdef PCNT_SELECTABLE
  if (_pcnt) {
    Log::console(PSTR("GeigerPulse: PCNT RXPIN: %d"), _rx_pin);
    // pull-up for PCNT init, user pull after
    gpio_set_pull_mode((gpio_num_t)_rx_pin, GPIO_PULLUP_ONLY);
    pcnt_begin(_rx_pin, GPIO_PULLUP_ONLY);
    set_pin_pull(_pin_pull);
    _polls = true;
    return;
  }
#endif
  Log::console(PSTR("GeigerPulse: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  IsrLock::pin_on_fall(_rx_pin, countInterrupt);
}

void GeigerPulse::stopForOTA() {
#ifdef PCNT_SELECTABLE
  if (_pcnt) {
    pcnt_counter_pause(PCNT_UNIT);
    return;
  }
#endif
  stopIsr();
}

void GeigerPulse::restartAfterOTA() {
#ifdef PCNT_SELECTABLE
  if (_pcnt) {
    pcnt_counter_resume(PCNT_UNIT);
    return;
  }
#endif
  restartIsr();
}

#ifdef PCNT_SELECTABLE
// wakes run it too, so it keeps its own time
void GeigerPulse::loop() {
  if (!_pcnt) return;
  uint32_t now = fast_millis();
  if (!due_every(now, _poll_at, DRAIN_MS)) return;
  drain_pcnt();
}

void GeigerPulse::drain_pcnt() {
  int16_t n = pcnt_take();
  if (n <= 0) return;

  uint32_t now_us = (uint32_t)micros();
  uint32_t span_us = now_us - _last_drain_us;
  if (span_us == 0 || span_us > 1000000UL) span_us = 1000000UL;
  _last_drain_us = now_us;

  IsrLock::enter(timerMux);
  s_event_counter += (uint32_t)n;
  IsrLock::leave(timerMux);

  _last_blip = now_us;
  Counter::on_pulse_batch((uint16_t)n, now_us, span_us);
}

int GeigerPulse::collect() {
  if (!_pcnt) return GeigerInput::collect();
  int16_t residual = pcnt_take();

  uint32_t now_us = (uint32_t)micros();

  if (residual > 0) {
    uint32_t span_us = now_us - _last_drain_us;
    if (span_us == 0 || span_us > 1000000UL) span_us = 1000000UL;
    Counter::on_pulse_batch((uint16_t)residual, now_us, span_us);
    _last_blip = now_us;
  }
  _last_drain_us = now_us;

  IsrLock::enter(timerMux);
  uint32_t pending = s_event_counter;
  s_event_counter = 0;
  IsrLock::leave(timerMux);

  return (int)(pending + (uint32_t)residual);
}

void GeigerPulse::set_pcnt_filter(int val) {
  _pcnt_filter = clamp(val, 0, 1023);
}

void GeigerPulse::apply_pcnt_filter() {
  if (!_pcnt) return;
  if (_pcnt_filter > 0) {
    pcnt_set_filter_value(PCNT_UNIT, _pcnt_filter);
    pcnt_filter_enable(PCNT_UNIT);
    Log::console(PSTR("GeigerPulse: PCNT filter set to %d"), _pcnt_filter);
  } else {
    pcnt_filter_disable(PCNT_UNIT);
    Log::console(PSTR("GeigerPulse: PCNT filter disabled"));
  }
}

// interrupt mode leaves the pin as begin set it
void GeigerPulse::set_pin_pull(int mode) {
  if (!_pcnt) return;
  gpio_pull_mode_t m;
  const char* name;
  switch (mode) {
    case PCNT_PULL_FLOATING: m = GPIO_FLOATING;        name = "floating"; break;
    case PCNT_PULL_DOWN:     m = GPIO_PULLDOWN_ONLY;   name = "pull-down"; break;
    case PCNT_PULL_UP:
    default:                 m = GPIO_PULLUP_ONLY;     name = "pull-up"; mode = PCNT_PULL_UP; break;
  }
  _pin_pull = mode;
  gpio_set_pull_mode((gpio_num_t)_rx_pin, m);
  if (mode != _pin_pull_last_logged) {
    Log::console(PSTR("GeigerPulse: PCNT pin pull = %s"), name);
    _pin_pull_last_logged = mode;
  }
}
#endif
#endif // GEIGER_IS_PULSE
