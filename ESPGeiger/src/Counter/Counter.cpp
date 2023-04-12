/*
  Counter.cpp - Geiger Counter class
  
  Copyright (C) 2023 @steadramon

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

#include <Arduino.h>
#include "Counter.h"
#include "../Logger/Logger.h"

Counter::Counter() {
}

int Counter::get_cpm() {
  return status.geigerTicks.get()*60;
}

int Counter::get_cpm5() {
  return status.geigerTicks5.get()*60;
}

int Counter::get_cpm15() {
  return status.geigerTicks15.get()*60;
}

void Counter::set_ratio(float ratio) {
  _ratio = ratio;
}

float Counter::get_usv() {
  float avgcpm = status.geigerTicks.get()*60;
  return avgcpm/_ratio;
}

void Counter::setup_pulse() {
#ifdef USE_PCNT && ESP32
  Log::console(PSTR("Counter: PCNT RXPIN: %d"), _geiger_rxpin);
  pcnt_config_t pcntConfig = {
    .pulse_gpio_num = _geiger_rxpin,
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
#else
  Log::console(PSTR("Counter: Interrupt RXPIN: %d"), _geiger_rxpin);
  pinMode(_geiger_rxpin, INPUT);
  attachInterrupt(digitalPinToInterrupt(_geiger_rxpin), count, FALLING);
#endif
}

void Counter::begin() {
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:%d 15:%d"), GEIGER_CPM_COUNT, GEIGER_CPM5_COUNT, GEIGER_CPM15_COUNT);
  status.geigerTicks.begin(SMOOTHED_AVERAGE, GEIGER_CPM_COUNT);
  status.geigerTicks5.begin(SMOOTHED_AVERAGE, GEIGER_CPM5_COUNT);
  status.geigerTicks15.begin(SMOOTHED_AVERAGE, GEIGER_CPM15_COUNT);

#if GEIGER_TYPE == GEIGER_TYPE_PULSE
  Log::console(PSTR("Counter: Setting up pulse geiger ..."));
  setup_pulse();
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
  Log::console(PSTR("Counter: Setting up serial geiger ..."));
  Log::console(PSTR("Counter: RXPIN: %d BAUD: %d"), _geiger_rxpin, _geiger_baud);
  geigerPort.begin(_geiger_baud, SWSERIAL_8N1, _geiger_rxpin, _geiger_txpin, false);
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
  Log::console(PSTR("Counter: Setting up test geiger ..."));
  #ifdef ESP8266
  timer1_attachInterrupt(count);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(600);
  #else
  hw_timer_t * timer = NULL;
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &count, true);
  timerAlarmWrite(timer, 600, true);
  timerAlarmEnable(timer);
  #endif
#elif GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
  Log::console(PSTR("Counter: Setting up test pulse geiger ..."));
  setup_pulse();
  Log::console(PSTR("Counter: Test TXPIN: %d"), _geiger_txpin);
  pinMode(_geiger_txpin, OUTPUT);
  #ifdef ESP8266
  timer1_attachInterrupt(sendpulse);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(300);
  #else
  hw_timer_t * timer = NULL;
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &sendpulse, true);
  timerAlarmWrite(timer, 300, true);
  timerAlarmEnable(timer);
  #endif
#endif

  geigerTicker.attach(1, handleSecondTick);
}

void Counter::loop() {
#if GEIGER_TYPE == GEIGER_TYPE_SERIAL
  if (geigerPort.available() > 0) {
    int n = geigerPort.readStringUntil('\r').toInt();
    Log::debug(PSTR("Counter loop - %d"), n);
    if (n) {
#if GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPM
      eventCounter = (float)n/(float)60;
#elif GEIGER_SERIAL_TYPE == GEIGER_SERIAL_CPS
      eventCounter = (float)n;
#endif
    }
    geigerPort.flush();
  }
#endif
}