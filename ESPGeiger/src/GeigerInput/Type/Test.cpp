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

#if GEIGER_IS_TEST(GEIGER_TYPE)

#include "Corium/Logger/Logger.h"
#include "TestTimer.h"

static volatile unsigned long _last_b;
// ticks, no libgcc in the isr
static volatile uint32_t _next_delay;

GeigerTest::GeigerTest() {
  strlcpy(_test_type, "TestGeiger", sizeof(_test_type));
};

void GeigerTest::begin() {
  GeigerInputTest::begin();
  set_debounce(0);
  CPMAdjuster();
  _next_delay = (uint32_t)calcDelay();
  TestTimer::start(testInterrupt, _next_delay);
}

void GeigerTest::secondTicker() {
  CPMAdjuster();
}

// returns ticks to the next
uint32_t IRAM_ATTR GeigerTest::testInterrupt() {
  GeigerInputTest::timerPulse();
  _last_b = micros();
  return _next_delay;
}

void GeigerTest::stopForOTA() { TestTimer::stop(); }

void GeigerTest::restartAfterOTA() { TestTimer::start(testInterrupt, _next_delay); }

void GeigerTest::loop() {
  countTimerPulses();
  if (_last_pulse_test != _last_b) {
    _last_pulse_test = _last_b;
    _next_delay = (uint32_t)calcDelay();   // one word, the isr reads it whole
  }
}
#endif // GEIGER_IS_TEST
