/*
  GeigerInput/Type/TestPulseInt.cpp - Class for Test Pulse Interrupt type counter

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

#if GEIGER_IS_TEST(GEIGER_TYPE)

#include "Corium/Logger/Logger.h"
#ifdef USE_PCNT
#include "Counter/Counter.h"
#endif
#include "TestTimer.h"

static volatile uint32_t s_half;   // ticks, isr reads it
static uint32_t s_tx_mask;   // 0 = no TX pin
static bool s_tx_hi;
static bool _bool_pulse_state = false;

GeigerTestPulseInt::GeigerTestPulseInt() {
  strlcpy(_test_type, "TestPulsePWM", sizeof(_test_type));
};

void GeigerTestPulseInt::begin() {
  GeigerInput::begin();
  Log::console(PSTR("TestPulsePWM: Setting up PWM test geiger ..."));
#ifdef GEIGER_COUNT_TXPULSE
  Log::console(PSTR("TestPulsePWM: TX pulse counting enabled"));
#endif

#ifdef USE_PCNT
  Log::console(PSTR("TestPulsePWM: PCNT RXPIN: %d"), _rx_pin);
  pcnt_begin(_rx_pin, GPIO_PULLDOWN_ONLY);
#else
  Log::console(PSTR("TestPulsePWM: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  IsrLock::pin_on_fall(_rx_pin, countInterrupt);
#endif

  Log::console(PSTR("TestPulsePWM: Test TXPIN: %d"), _tx_pin);
  if (TestTimer::tx_pin(_tx_pin, s_tx_mask, s_tx_hi)) {
    pinMode(_tx_pin, OUTPUT);
  } else if (_tx_pin >= 0) {
    Log::console(PSTR("TestPulsePWM: TXPIN %d can't be driven, no TX"), _tx_pin);
  }
  CPMAdjuster();
  _target_pwm = calcPWM();
  s_half = (uint32_t)_target_pwm;
  TestTimer::start(pulseInterrupt, s_half);
  _current_pwm = _target_pwm;
}

void GeigerTestPulseInt::loop() {
  take();
  countTimerPulses();
}

void GeigerTestPulseInt::stopForOTA() { TestTimer::stop(); }

void GeigerTestPulseInt::restartAfterOTA() { TestTimer::start(pulseInterrupt, s_half); }

// returns ticks to the next
uint32_t EG_ISR_ATTR GeigerTestPulseInt::pulseInterrupt() {
  _bool_pulse_state = !_bool_pulse_state;
  TestTimer::tx(s_tx_mask, s_tx_hi, _bool_pulse_state);
#ifdef GEIGER_COUNT_TXPULSE
  if (!_bool_pulse_state) GeigerInputTest::timerPulse();
#endif
  return s_half;
}

double GeigerTestPulseInt::calcPWM() {
  double r = (GEIGER_TEST_TIMER_DIV / GeigerInputTest::getTargetCPS()) / 2;
  // 0 re-fires nonstop
  if (r < _pulse_width_ticks) r = _pulse_width_ticks;
  if (r < 1) r = 1;
  if (r > UINT32_MAX) r = UINT32_MAX;
  return r;
}

void GeigerTestPulseInt::secondTicker() {
#ifndef USE_PCNT
  checkIsrStorm();
#endif
  CPMAdjuster();
  _target_pwm = calcPWM();
  if (_target_pwm != _current_pwm) {
    s_half = (uint32_t)_target_pwm;   // next edge picks it up
    _current_pwm = _target_pwm;
  }
}

#ifdef USE_PCNT
// s_event_counter stays 0, UdpBlip adds it to total_clicks
int GeigerTestPulseInt::collect() {
#ifdef GEIGER_COUNT_TXPULSE
  pcnt_take();   // keep the unit empty
  return GeigerInput::collect();
#else
  int16_t n = pcnt_take();
  if (n > 0) {
    setLastBlip();
    Counter::on_pulse_batch((uint16_t)n, (uint32_t)micros(), 1000000UL);
  }
  return n;
#endif
}
#endif
#endif // GEIGER_IS_TEST
