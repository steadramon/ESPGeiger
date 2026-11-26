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

#if GEIGER_TYPE == GEIGER_TYPE_TESTPULSE

#include "Corium/Logger/Logger.h"
#ifdef USE_PCNT
#include "Counter/Counter.h"
#endif
#include "TestTimer.h"
#include "Corium/System/FastPin.h"

static uint32_t s_tx_mask;   // 0 = no TX pin
static bool s_tx_hi;
static volatile bool s_high = false;   // loop reads it
static volatile unsigned long s_blip_us;
// ticks, no libgcc in the isr
static volatile uint32_t s_this;
static volatile uint32_t s_next;

GeigerTestPulse::GeigerTestPulse() {
  strlcpy(_test_type, "TestPulse", sizeof(_test_type));
};

void GeigerTestPulse::begin() {
  GeigerInput::begin();
  corium::Log::console(PSTR("TestPulse: Setting up Poisson test geiger ..."));
#ifdef GEIGER_COUNT_TXPULSE
  corium::Log::console(PSTR("TestPulse: TX pulse counting enabled"));
#endif

#ifdef USE_PCNT
  corium::Log::console(PSTR("TestPulse: PCNT RXPIN: %d"), _rx_pin);
  pcnt_begin(_rx_pin, GPIO_PULLDOWN_ONLY);
#else
  corium::Log::console(PSTR("TestPulse: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  corium::IsrLock::pinOnFall(_rx_pin, countInterrupt);
#endif

  corium::Log::console(PSTR("TestPulse: Test TXPIN: %d"), _tx_pin);
  if (corium::FastPin::bits(_tx_pin, s_tx_mask, s_tx_hi)) {
    pinMode(_tx_pin, OUTPUT);
  } else if (_tx_pin >= 0) {
    corium::Log::console(PSTR("TestPulse: TXPIN %d can't be driven, no TX"), _tx_pin);
  }
  CPMAdjuster();
  s_next = (uint32_t)calcDelay();
  s_this = s_next;
  TestTimer::start(pulseInterrupt, s_next);
}

void GeigerTestPulse::stopForOTA() { TestTimer::stop(); }

void GeigerTestPulse::restartAfterOTA() { TestTimer::start(pulseInterrupt, s_next); }

void GeigerTestPulse::loop() {
  take();
  countTimerPulses();
  if (_retarget) {
    // Only retarget between pulses, try again next pass if the pin is high
    if (s_high) {
      post();
    } else {
      _retarget = false;
      s_next = (uint32_t)calcDelay();
      TestTimer::restart((uint32_t)calcDelay());
    }
  }
  if (_last_pulse_test != s_blip_us) {
    _last_pulse_test = s_blip_us;
    s_next = (uint32_t)calcDelay();   // one word, the isr reads it whole
  }
}

// returns ticks to the next edge
uint32_t CORIUM_ISR_ATTR GeigerTestPulse::pulseInterrupt() {
  s_high = !s_high;
  corium::FastPin::write(s_tx_mask, s_tx_hi, s_high);
  uint32_t ticks = 0;
  uint32_t width = _pulse_width_ticks;
  if (!s_high) {
#ifdef GEIGER_COUNT_TXPULSE
    GeigerInputTest::timerPulse();
#endif
    s_blip_us = micros();
    GeigerInputTest::post();
    // low time = gap less the width, min one width
    uint32_t next = s_next;
    s_this = (next > 2 * width) ? next - width : width;
    ticks = s_this;
  } else {
    ticks = width;
  }
  return ticks;
}

void GeigerTestPulse::secondTicker() {
#ifndef USE_PCNT
  checkIsrStorm();
#endif
  CPMAdjuster();
}

#ifdef USE_PCNT
// s_event_counter stays 0, UdpBlip adds it to total_clicks
int GeigerTestPulse::collect() {
  int16_t n = pcnt_take();
  if (n > 0) {
    setLastBlip();
    Counter::on_pulse_batch((uint16_t)n, (uint32_t)micros(), 1000000UL);
  }
  return n;
}
#endif
#endif // GEIGER_TYPE_TESTPULSE
