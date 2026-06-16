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

#include "../../Logger/Logger.h"
#include "../../Util/MathUtil.h"
#ifdef USE_PCNT
#include "../../Counter/Counter.h"   // Counter::on_pulse_batch ring synth (PCNT)
#include "../../Util/FastMillis.h"   // fast_millis() - real millis() halves LPS
#endif

GeigerPulse::GeigerPulse() {
};

void GeigerPulse::begin() {
  GeigerInput::begin();
#ifdef USE_PCNT
  Log::console(PSTR("GeigerPulse: PCNT RXPIN: %d"), _rx_pin);
  // Force pull-up during PCNT init: the channel counts on falling edges,
  // so the edge detector needs a HIGH prior-state to latch against. We
  // restore the user-configured pull after the counter is running.
  gpio_set_pull_mode((gpio_num_t)_rx_pin, GPIO_PULLUP_ONLY);
  pcnt_config_t pcntConfig = {
    .pulse_gpio_num = _rx_pin,
    .ctrl_gpio_num = -1,
    .pos_mode = PCNT_CHANNEL_EDGE_ACTION_HOLD,
    .neg_mode = PCNT_CHANNEL_EDGE_ACTION_INCREASE,
    .counter_h_lim = PCNT_HIGH_LIMIT,
    .counter_l_lim = PCNT_LOW_LIMIT,
    .unit = PCNT_UNIT,
    .channel = PCNT_CHANNEL,
  };
  pcnt_unit_config(&pcntConfig);
  pcnt_counter_pause(PCNT_UNIT);
  #ifdef PCNT_FILTER
  pcnt_set_filter_value(PCNT_UNIT, PCNT_FILTER);
  pcnt_filter_enable(PCNT_UNIT);
  #endif
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
  set_pin_pull(_pin_pull);
#else
  Log::console(PSTR("GeigerPulse: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(_rx_pin), countInterrupt, FALLING);
#endif
}

void GeigerPulse::stopForOTA() {
#ifdef USE_PCNT
  pcnt_counter_pause(PCNT_UNIT);
#else
  detachInterrupt(digitalPinToInterrupt(_rx_pin));
#endif
}

void GeigerPulse::restartAfterOTA() {
#ifdef USE_PCNT
  pcnt_counter_resume(PCNT_UNIT);
#else
  attachInterrupt(digitalPinToInterrupt(_rx_pin), countInterrupt, FALLING);
#endif
}

#ifdef USE_PCNT
void GeigerPulse::drain_pcnt() {
  // Caller throttles to 50 Hz; this assumes a real drain is wanted.
  int16_t n = 0;
  pcnt_counter_pause(PCNT_UNIT);
  pcnt_get_counter_value(PCNT_UNIT, &n);
  if (n <= 0) {
    pcnt_counter_resume(PCNT_UNIT);
    return;
  }
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);

  uint32_t now_us = (uint32_t)micros();
  uint32_t span_us = now_us - _last_drain_us;
  if (span_us == 0 || span_us > 1000000UL) span_us = 1000000UL;
  _last_drain_us = now_us;

  portENTER_CRITICAL(&timerMux);
  s_event_counter += (uint32_t)n;
  portEXIT_CRITICAL(&timerMux);

  _last_blip = now_us;
  Counter::on_pulse_batch((uint16_t)n, now_us, span_us);
}

int GeigerPulse::collect() {
  int16_t residual = 0;
  pcnt_counter_pause(PCNT_UNIT);
  pcnt_get_counter_value(PCNT_UNIT, &residual);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);

  uint32_t now_us = (uint32_t)micros();

  if (residual > 0) {
    uint32_t span_us = now_us - _last_drain_us;
    if (span_us == 0 || span_us > 1000000UL) span_us = 1000000UL;
    Counter::on_pulse_batch((uint16_t)residual, now_us, span_us);
    _last_blip = now_us;
  }
  _last_drain_us = now_us;

  portENTER_CRITICAL(&timerMux);
  uint32_t pending = s_event_counter;
  s_event_counter = 0;
  portEXIT_CRITICAL(&timerMux);

  return (int)(pending + (uint32_t)residual);
}

void GeigerPulse::set_pcnt_filter(int val) {
  _pcnt_filter = clamp(val, 0, 1023);
}

void GeigerPulse::apply_pcnt_filter() {
  if (_pcnt_filter > 0) {
    pcnt_set_filter_value(PCNT_UNIT, _pcnt_filter);
    pcnt_filter_enable(PCNT_UNIT);
    Log::console(PSTR("GeigerPulse: PCNT filter set to %d"), _pcnt_filter);
  } else {
    pcnt_filter_disable(PCNT_UNIT);
    Log::console(PSTR("GeigerPulse: PCNT filter disabled"));
  }
}

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
  if (mode != _pin_pull_last_logged) {
    Log::console(PSTR("GeigerPulse: PCNT pin pull = %s"), name);
    _pin_pull_last_logged = mode;
  }
}
#endif
#endif // GEIGER_IS_PULSE
