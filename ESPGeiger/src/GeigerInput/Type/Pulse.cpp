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
#include "../../Logger/Logger.h"

GeigerPulse::GeigerPulse() {
};

void GeigerPulse::begin() {
  GeigerInput::begin();
#ifdef USE_PCNT
  Log::console(PSTR("GeigerPulse: PCNT RXPIN: %d"), _rx_pin);
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
  gpio_set_pull_mode((gpio_num_t)_rx_pin, PCNT_PIN_PULL_MODE);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
#else
  Log::console(PSTR("GeigerPulse: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(_rx_pin), countInterrupt, FALLING);
#endif
}

#ifdef USE_PCNT
int GeigerPulse::collect() {
  int16_t pulseCount;
  pcnt_counter_pause(PCNT_UNIT);
  pcnt_get_counter_value(PCNT_UNIT, &pulseCount);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
  if (pulseCount != 0) {
    setCounter(pulseCount);
  } else {
    setCounter(pulseCount, false);
  }
  return pulseCount;
}

void GeigerPulse::set_pcnt_filter(int val) {
  if (val < 0) val = 0;
  if (val > 1023) val = 1023;
  _pcnt_filter = val;
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
#endif
