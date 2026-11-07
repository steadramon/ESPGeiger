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

#if GEIGER_TYPE == GEIGER_TYPE_TEST

#include "TestTimer.h"

static volatile unsigned long s_blip_us;
// In ticks, so the isr needs no libgcc maths
static volatile uint32_t s_next;

GeigerTest::GeigerTest() {
  strlcpy(_test_type, "TestGeiger", sizeof(_test_type));
};

void GeigerTest::begin() {
  GeigerInputTest::begin();
  set_debounce(0);
  CPMAdjuster();
  s_next = (uint32_t)calcDelay();
  TestTimer::start(testInterrupt, s_next);
}

void GeigerTest::secondTicker() {
  CPMAdjuster();
}

// Returns ticks to the next pulse
uint32_t CORIUM_ISR_ATTR GeigerTest::testInterrupt() {
  GeigerInputTest::timerPulse();
  s_blip_us = micros();
  return s_next;
}

void GeigerTest::stopForOTA() { TestTimer::stop(); }

void GeigerTest::restartAfterOTA() { TestTimer::start(testInterrupt, s_next); }

void GeigerTest::loop() {
  take();
  countTimerPulses();
  if (_retarget) {
    _retarget = false;
    s_next = (uint32_t)calcDelay();
    TestTimer::restart((uint32_t)calcDelay());
  }
  if (_last_pulse_test != s_blip_us) {
    _last_pulse_test = s_blip_us;
    s_next = (uint32_t)calcDelay();   // one word, the isr reads it whole
  }
}
#endif // GEIGER_TYPE_TEST
