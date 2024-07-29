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
  strcpy(_test_type, "TestPulsePWM");
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
  _pulse_tx_pin = _tx_pin;
  CPMAdjuster();
  _target_pwm = calcPWM();

#ifdef ESP8266
  timer1_attachInterrupt(pulseInterrupt);
  timer1_isr_init();
  timer1_enable(GEIGER_TEST_TIMER_FREQ, TIM_EDGE, TIM_LOOP);
  timer1_write(_target_pwm);
#else
  const esp_timer_create_args_t cfg_et = {
    .callback = &pulseInterrupt,
    .name = "pulseint",
  };
  esp_timer_create(&cfg_et, &hdl_pulse_timer);
  esp_timer_start_periodic(hdl_pulse_timer, _target_pwm);
#endif
  _current_pwm = _target_pwm;
}

void GeigerTestPulseInt::loop() {
}

void GeigerTestPulseInt::pulseInterrupt(void *data) {
  pulseInterrupt();
}

void GeigerTestPulseInt::pulseInterrupt() {
  _bool_pulse_state = !_bool_pulse_state;
#ifdef ESP8266
  if (_bool_pulse_state) {
    GPOS = (1 << _pulse_tx_pin);
  } else {
    GPOC = (1 << _pulse_tx_pin);
  }
#else
  if (_bool_pulse_state) {
    GPIO.out_w1ts = ((uint32_t)1 << _pulse_tx_pin); // high
  } else {
    GPIO.out_w1tc = ((uint32_t)1 << _pulse_tx_pin); // low
  }
#endif
  if (!_bool_pulse_state) {
#ifdef GEIGER_COUNT_TXPULSE
    GeigerInputTest::countInterrupt();
#endif
  }
}

double GeigerTestPulseInt::calcPWM() {
  return (GEIGER_TEST_TIMER_DIV/GeigerInputTest::getTargetCPS()) / 2;
}

void GeigerTestPulseInt::secondTicker() {
  CPMAdjuster();
  _target_pwm = calcPWM();
  if (_target_pwm != _current_pwm) {
#ifdef ESP8266
    timer1_write(_target_pwm);
#else
    esp_timer_stop(hdl_pulse_timer);
    esp_timer_start_periodic(hdl_pulse_timer, _target_pwm);
#endif
    _current_pwm = _target_pwm;
  }
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
