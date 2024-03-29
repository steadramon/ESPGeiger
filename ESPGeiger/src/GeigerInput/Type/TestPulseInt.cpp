/*
  GeigerInput/Type/TestPulseInt.cpp - Class for Test Pulse type counter
  
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
#include "TestPulseInt.h"
#include "../../Logger/Logger.h"

GeigerTestPulseInt::GeigerTestPulseInt() {
};

void GeigerTestPulseInt::begin() {
  GeigerInput::begin();
  Log::console(PSTR("TestPulsePWM: Setting up test PWM geiger ..."));

#ifdef USE_PCNT && ESP32
  Log::console(PSTR("TestPulsePWM: PCNT RXPIN: %d"), _rx_pin);
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
  Log::console(PSTR("TestPulsePWM: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(_rx_pin), countInterrupt, FALLING);
#endif

  Log::console(PSTR("TestPulsePWM: Test TXPIN: %d"), _tx_pin);
  pinMode(_tx_pin, OUTPUT);
  setTargetCPS(GEIGER_TEST_INITIAL_CPS);

#ifdef ESP8266
  timer1_attachInterrupt(pulseInterrupt);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  timer1_write(_target_pwm);
#else
  pulsetimer = timerBegin(0, 80, true);
  timerAttachInterrupt(pulsetimer, pulseInterrupt, true);
  timerAlarmWrite(pulsetimer, _target_pwm, true);
  timerAlarmEnable(pulsetimer);
#endif
}

void GeigerTestPulseInt::loop() {
  if (_pulse_send == true) {
#ifdef ESP8266
//    digitalWrite(_tx_pin, _bool_pulse_state);
    if (_bool_pulse_state) {
      GPOS = (1 << _tx_pin);
    } else {
      GPOC = (1 << _tx_pin);
    }
#else
    if (_bool_pulse_state) {
      GPIO.out_w1ts = ((uint32_t)1 << _tx_pin);
    } else {
      GPIO.out_w1tc = ((uint32_t)1 << _tx_pin);
    }
#endif
    _bool_pulse_state = !_bool_pulse_state;
    _pulse_send = false;
    if (_bool_pulse_state) {
#ifdef GEIGER_COUNT_TXPULSE
      countInterrupt();
#endif
    }
  }
}

void GeigerTestPulseInt::pulseInterrupt() {
  _pulse_send = true;
}

void GeigerTestPulseInt::setTargetCPM(float target) {
  setTargetCPS(target/60.0);
};

void GeigerTestPulseInt::setTargetCPS(float target) {
#ifdef ESP8266
  _target_pwm = (int)2500000/target;
#else
  _target_pwm = (int)500000/target;
#endif
};

void GeigerTestPulseInt::CPMAdjuster() {
#ifndef GEIGER_TESTPULSE_FIXEDCPM
  int selection = (millis() / GEIGER_TESTPULSE_ADJUSTTIME) % 4;
  if (selection != _current_selection) {
    _current_selection = selection;
    int _targetCPM = 30;
    switch (selection) {
      case 1:
        _targetCPM = 60;
        break;
      case 2:
        _targetCPM = 100;
        break;
      case 3:
        _targetCPM = 120;
        break;
      default:
        _targetCPM = 30;
        break;
    }
#ifdef GEIGER_TEST_FAST
    _targetCPM = _targetCPM * 100;
#endif
    Log::console(PSTR("TestPulsePWM: Setting CPM to: %d"), _targetCPM);
    setTargetCPM(_targetCPM);
#ifdef ESP8266
    timer1_write(_target_pwm);
#else
    timerAlarmWrite(pulsetimer, _target_pwm, true);
#endif
  }
#endif
}

void GeigerTestPulseInt::secondTicker() {
  CPMAdjuster();
}

#ifdef USE_PCNT && ESP32
int GeigerTestPulseInt::collect() {
  int16_t pulseCount;
  pcnt_get_counter_value(PCNT_UNIT, &pulseCount);
  pcnt_counter_clear(PCNT_UNIT);
#ifdef GEIGER_COUNT_TXPULSE
  return GeigerInput::collect();
#endif
  if (pulseCount != 0) {
    setCounter(pulseCount);
  } else {
    setCounter(pulseCount, false);
  }
  return pulseCount;
}
#endif