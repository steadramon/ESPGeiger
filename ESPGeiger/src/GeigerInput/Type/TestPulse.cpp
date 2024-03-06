/*
  GeigerInput/Type/TestPulse.cpp - Class for Test Pulse type counter
  
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
#include "TestPulse.h"
#include "../../Logger/Logger.h"

GeigerTestPulse::GeigerTestPulse() {
};

void GeigerTestPulse::begin() {
  GeigerInput::begin();
  Log::console(PSTR("GeigerPulse: Setting up test geiger ..."));

#ifdef USE_PCNT && ESP32
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
  gpio_set_pull_mode((gpio_num_t)_rx_pin, GPIO_PULLDOWN_ONLY);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
#else
  Log::console(PSTR("GeigerPulse: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(_rx_pin), countInterrupt, FALLING);
#endif

  Log::console(PSTR("GeigerPulse: Test TXPIN: %d"), _tx_pin);
  pinMode(_tx_pin, OUTPUT);

#ifdef ESP8266
  timer1_attachInterrupt(pulseInterrupt);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
#ifdef GEIGER_TEST_FAST
  // 1666.66 CPS / 100000 CPM
  timer1_write(1500);
#else
  // 0.5 CPS / 30 CPM
  timer1_write(5000000);
#endif
#else
  hw_timer_t * timer = NULL;
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, pulseInterrupt, true);
  timerAlarmWrite(timer, 300, true);
  timerAlarmEnable(timer);
#endif
}

void GeigerTestPulse::loop() {
  if (_pulse_send == true) {
    digitalWrite(_tx_pin, _bool_pulse_state);
    _bool_pulse_state = !_bool_pulse_state;
    _pulse_send = false;
#ifdef GEIGER_COUNT_TXPULSE
    if (_bool_pulse_state) {
      countInterrupt();
    }
#endif
  }
}

void GeigerTestPulse::pulseInterrupt() {
  _pulse_send = true;
}

void GeigerTestPulse::secondticker() {
#ifndef GEIGER_TESTPULSE_FIXEDCPM
  int selection = (millis() / GEIGER_TESTPULSE_ADJUSTTIME) % 4;
  if (selection != _current_selection) {
    _current_selection = selection;
#ifdef ESP8266
    switch (selection) {
      case 1:
        // 1 CPS / 60 CPM
        timer1_write(2500000);
        break;
      case 2:
        // 1.66 CPS / 100 CPM
        timer1_write(1500000);
        break;
      case 3:
        // 2 CPS / 120 CPM
        timer1_write(1250000);
        break;
      default:
        // 0.5 CPS / 30 CPM
        timer1_write(5000000);
        break;
    }
#endif
  }
#endif
}

#ifdef USE_PCNT && ESP32
int GeigerTestPulse::collect() {
  int16_t pulseCount;
  pcnt_get_counter_value(PCNT_UNIT, &pulseCount);
  pcnt_counter_clear(PCNT_UNIT);
  setCounter(pulseCount);
  return pulseCount;
}
#endif