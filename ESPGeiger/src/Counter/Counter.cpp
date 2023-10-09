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
  status.geiger_model = GEIGER_MODEL;
}

float Counter::get_cps() {
  return status.geigerTicks.get();
}

int Counter::get_cpm() {
  return status.geigerTicks.get()*60;
}

float Counter::get_cpmf() {
  return status.geigerTicks.get()*60.0;
}

int Counter::get_cpm5() {
  return status.geigerTicks5.get()*60;
}

float Counter::get_cpm5f() {
  return status.geigerTicks5.get()*60.0;
}

int Counter::get_cpm15() {
  return status.geigerTicks15.get()*60;
}

float Counter::get_cpm15f() {
  return status.geigerTicks15.get()*60.0;
}

void Counter::set_ratio(float ratio) {
  _ratio = ratio;
}

void Counter::set_rx_pin(int pin) {
  _geiger_rxpin = pin;
}
void Counter::set_tx_pin(int pin) {
  _geiger_txpin = pin;
}

int Counter::get_rx_pin() {
  return _geiger_rxpin;
}

int Counter::get_tx_pin() {
  return _geiger_txpin;
}

float Counter::get_usv() {
  float avgcpm = status.geigerTicks.get()*60;
  return avgcpm/_ratio;
}

#if GEIGER_TYPE == GEIGER_TYPE_PULSE || GEIGER_TYPE == GEIGER_TYPE_TESTPULSE
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
  gpio_set_pull_mode((gpio_num_t)_geiger_rxpin, GPIO_PULLDOWN_ONLY);
  pcnt_counter_clear(PCNT_UNIT);
  pcnt_counter_resume(PCNT_UNIT);
#else
  Log::console(PSTR("Counter: Interrupt RXPIN: %d"), _geiger_rxpin);
  pinMode(_geiger_rxpin, INPUT);
  attachInterrupt(digitalPinToInterrupt(_geiger_rxpin), count, FALLING);
#endif
}
#endif

void Counter::begin() {
  Log::console(PSTR("Counter: Bucket sizes - 1:%d 5:%d 15:%d"), GEIGER_CPM_COUNT, GEIGER_CPM5_COUNT, GEIGER_CPM15_COUNT);
  status.geigerTicks.begin(SMOOTHED_AVERAGE, GEIGER_CPM_COUNT);
  status.geigerTicks5.begin(SMOOTHED_AVERAGE, GEIGER_CPM5_COUNT);
  status.geigerTicks15.begin(SMOOTHED_AVERAGE, GEIGER_CPM15_COUNT);

#if GEIGER_TYPE == GEIGER_TYPE_PULSE
  Log::console(PSTR("Counter: Setting up pulse geiger ..."));
  setup_pulse();
#elif GEIGER_TYPE == GEIGER_TYPE_SERIAL
  Log::console(PSTR("Counter: Setting up %s serial geiger ..."), GEIGER_MODEL);
  Log::console(PSTR("Counter: RXPIN: %d BAUD: %d"), _geiger_rxpin, GEIGER_BAUDRATE);
  geigerPort.begin(GEIGER_BAUDRATE, SWSERIAL_8N1, _geiger_rxpin, _geiger_txpin, false);
#elif GEIGER_TYPE == GEIGER_TYPE_TEST
  Log::console(PSTR("Counter: Setting up test geiger ..."));
  #ifdef ESP8266
  timer1_attachInterrupt(count);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(600);
  #else
  hwtimer = timerBegin(0, 80, true);
  timerAlarmEnable(hwtimer);
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
#elif GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
  Log::console(PSTR("Counter: Setting up test %s serial geiger ..."), GEIGER_MODEL);
  Log::console(PSTR("Counter: RXPIN: %d BAUD: %d"), _geiger_rxpin, GEIGER_BAUDRATE);
  Log::console(PSTR("Counter: TXPIN: %d BAUD: %d"), _geiger_txpin, GEIGER_BAUDRATE);
  geigerPort.begin(GEIGER_BAUDRATE, SWSERIAL_8N1, _geiger_rxpin, _geiger_txpin, false);
#endif

  geigerTicker.attach(1, handleSecondTick);
}

void Counter::handleSerial(char* input)
{
#if GEIGER_TYPE == GEIGER_TYPE_SERIAL || GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
  int _scpm;
#if GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
  int _scps;
    int n = sscanf(input, "CPS, %d, CPM, %d", &_scps, &_scpm);
    if (n == 2) {
#else
    int n = sscanf(input, "%d\n", &_scpm);
    if (n == 1) {
#endif
      Log::debug(PSTR("Counter: Loop - %d"), _scpm);
#ifndef DISABLE_SERIALBLIP
      _last_blip = ESP.getCycleCount();
#endif
      if (_handlesecond)
        return;
#ifdef ESP32
  portENTER_CRITICAL(&timerMux);
#endif
      eventCounter = _scpm;
#ifdef ESP32
  portEXIT_CRITICAL(&timerMux);
#endif
    }
#endif
}

void Counter::loop() {
#ifndef DISABLE_BLIP
  if (status.last_blip != _last_blip) {
    status.last_blip = _last_blip;
#ifdef ESPGEIGER_HW
    status.blip_led.Blink(2,2);
#else
    status.led.Blink(20,20);
#endif
  }
#endif
#if GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
  unsigned long int secidx = (millis() / 1000);
  if (secidx != _last_secidx) {
    _last_secidx = secidx;
  #if GEIGER_SERIALTYPE == GEIGER_STYPE_MIGHTYOHM
    geigerPort.print(PSTR("CPS, 1, CPM, 60, uSv/hr, 1.23, INST\n"));
  #else
    geigerPort.print(PSTR("60\n"));
  #endif
  }
#endif
#if GEIGER_TYPE == GEIGER_TYPE_SERIAL || GEIGER_TYPE == GEIGER_TYPE_TESTSERIAL
  if (geigerPort.available() > 0) {
    if (geigerPort.overflow()) {
      Log::console(PSTR("Counter: Serial Overflow %d"), geigerPort.available());
    }
    char input = geigerPort.read();
    _serial_buffer[_serial_idx++] = input;
    if (input == '\n') {
      handleSerial(_serial_buffer);
      _serial_idx = 0;
    }

    if (_serial_idx > 52) {
      _serial_idx = 0;
    }
  }
#endif
}