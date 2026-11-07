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

#if GEIGER_TYPE == GEIGER_TYPE_PULSE

#include "Corium/Logger/Logger.h"
#include "Corium/Util/MathUtil.h"
#ifdef PCNT_SELECTABLE
#include "Counter/Counter.h"
#endif

#ifdef NO_PCNT_ALIAS
// a _no_pcnt device's /update looks for its own env
static const char NO_PCNT_TAG[] PROGMEM = "<EGENV:" BUILD_ENV "_no_pcnt:ENV>";
#endif

GeigerPulse::GeigerPulse() {
};

void GeigerPulse::begin() {
  GeigerInput::begin();
#ifdef NO_PCNT_ALIAS
  asm volatile("" : : "r"(NO_PCNT_TAG));   // keep it linked
#endif
#ifdef PCNT_SELECTABLE
  if (_pcnt) {
    corium::Log::console(PSTR("GeigerPulse: PCNT RXPIN: %d"), _rx_pin);
    // pull-up for PCNT init, user pull after
    gpio_set_pull_mode((gpio_num_t)_rx_pin, GPIO_PULLUP_ONLY);
    pcnt_begin(_rx_pin, GPIO_PULLUP_ONLY);
    set_pin_pull(_pin_pull);
    _polls = true;
    return;
  }
#endif
  corium::Log::console(PSTR("GeigerPulse: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
#ifdef PCNT_SELECTABLE
  set_pin_pull(_pin_pull);
#endif
  corium::IsrLock::pinOnFall(_rx_pin, countInterrupt);
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
// us since the last drain, 1 s the first time; a stall's pulses spread over the stall
static inline __attribute__((always_inline)) uint32_t span_us(int64_t now_us, int64_t last_us) {
  if (last_us == 0 || now_us <= last_us) return 1000000UL;
  int64_t s = now_us - last_us;
  return s < (int64_t)UINT32_MAX ? (uint32_t)s : UINT32_MAX;
}

// wakes run it too, so it keeps its own time
void GeigerPulse::loop() {
  if (!_pcnt) return;
  uint32_t now = corium::fastMillis();
  if (!corium::dueEvery(now, _poll_at, DRAIN_MS)) return;
  drain_pcnt();
}

void GeigerPulse::drain_pcnt() {
  int16_t n = pcnt_take();
  if (n <= 0) return;

  int64_t now = esp_timer_get_time();
  uint32_t span = span_us(now, _last_drain_us);
  _last_drain_us = now;
  uint32_t now_us = (uint32_t)now;

  corium::IsrLock::enter(timerMux);
  s_event_counter += (uint32_t)n;
  corium::IsrLock::leave(timerMux);

  _last_blip = now_us;
  _pushed += Counter::on_pulse_batch((uint16_t)n, now_us, span);
}

int GeigerPulse::collect() {
  if (!_pcnt) return GeigerInput::collect();
  int16_t residual = pcnt_take();

  int64_t now = esp_timer_get_time();
  uint32_t now_us = (uint32_t)now;

  if (residual > 0) {
    _pushed += Counter::on_pulse_batch((uint16_t)residual, now_us, span_us(now, _last_drain_us));
    _last_blip = now_us;
  }
  _last_drain_us = now;
  uint32_t pushed = _pushed;
  _pushed = 0;

  corium::IsrLock::enter(timerMux);
  uint32_t pending = s_event_counter;
  s_event_counter = 0;
  corium::IsrLock::leave(timerMux);

  uint32_t n = pending + (uint32_t)residual;
  if (n > STORM_COUNTS) {
    Counter::drop_pulses(pushed);   // what this second put in, not n: older ones stay
    if (!_storm) corium::Log::console(PSTR("GeigerPulse: PCNT storm (%u/s) - dropped"), (unsigned)n);
    _storm = true;
    return 0;
  }
  _storm = false;
  return (int)n;
}

void GeigerPulse::set_pcnt_filter(int val) {
  _pcnt_filter = clamp(val, 0, 1023);
}

void GeigerPulse::apply_pcnt_filter() {
  if (!_pcnt || !_polls) return;   // no unit till begin
  if (_pcnt_filter > 0) {
    pcnt_set_filter_value(PCNT_UNIT, _pcnt_filter);
    pcnt_filter_enable(PCNT_UNIT);
    corium::Log::console(PSTR("GeigerPulse: PCNT filter set to %d"), _pcnt_filter);
  } else {
    pcnt_filter_disable(PCNT_UNIT);
    corium::Log::console(PSTR("GeigerPulse: PCNT filter disabled"));
  }
}

// plain gpio, both modes
void GeigerPulse::set_pin_pull(int mode) {
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
  if (mode != _pull_logged) {
    corium::Log::console(PSTR("GeigerPulse: PCNT pin pull = %s"), name);
    _pull_logged = mode;
  }
}
#endif
#endif // GEIGER_TYPE_PULSE
