/*
  GeigerInput/Type/Test.cpp - Class for Test type counter
  
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
#include "Test.h"
#include "../../Logger/Logger.h"

GeigerTest::GeigerTest() {
  strcpy(_test_type, "TestGeiger");
};

void GeigerTest::begin() {
  GeigerInputTest::begin();
  CPMAdjuster();
  _next_delay = calcDelay();
#ifdef ESP8266
  timer1_attachInterrupt(testInterrupt);
  timer1_isr_init();
  timer1_enable(GEIGER_TEST_TIMER_FREQ, TIM_EDGE, TIM_SINGLE);
  timer1_write((unsigned long)_next_delay);
#else
  const esp_timer_create_args_t cfg_et = {
    .callback = &testInterrupt,
    .name = "testint",
  };
  esp_timer_create(&cfg_et, &hdl_pulse_timer);
  esp_timer_start_once(hdl_pulse_timer, (unsigned long)_next_delay);
#endif
}

void GeigerTest::secondTicker() {
  CPMAdjuster();
}

void GeigerTest::testInterrupt(void *data) {
  testInterrupt();
}

void ICACHE_RAM_ATTR GeigerTest::testInterrupt() {
  GeigerInputTest::countInterrupt();
#ifdef ESP8266
  timer1_write((unsigned long)_next_delay);
#else
  esp_timer_start_once(hdl_pulse_timer, (unsigned long)_next_delay);
#endif
  _last_b = micros();
}

void GeigerTest::loop() {
  if (_last_pulse_test != _last_b) {
    _last_pulse_test = _last_b;
#ifdef ESP8266
    _next_delay = calcDelay();
#else
    portENTER_CRITICAL_ISR(&timerMux);
    _next_delay = calcDelay();
    portEXIT_CRITICAL_ISR(&timerMux);
#endif
  }
}
