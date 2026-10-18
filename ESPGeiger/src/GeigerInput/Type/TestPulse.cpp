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

#if GEIGER_IS_TEST(GEIGER_TYPE)

#include "Corium/Logger/Logger.h"
#ifdef USE_PCNT
#include "Counter/Counter.h"
#endif
#include "TestTimer.h"

static uint32_t s_tx_mask;   // 0 = no TX pin
static bool s_tx_hi;
static bool _bool_pulse_state = false;
static volatile unsigned long _last_b;
// ticks, no libgcc in the isr
static volatile uint32_t _this_delay;
static volatile uint32_t _next_delay;

GeigerTestPulse::GeigerTestPulse() {
  strlcpy(_test_type, "TestPulse", sizeof(_test_type));
};

void GeigerTestPulse::begin() {
  GeigerInput::begin();
  Log::console(PSTR("TestPulse: Setting up Poisson test geiger ..."));
#ifdef GEIGER_COUNT_TXPULSE
  Log::console(PSTR("TestPulse: TX pulse counting enabled"));
#endif

#ifdef USE_PCNT
  Log::console(PSTR("TestPulse: PCNT RXPIN: %d"), _rx_pin);
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
  Log::console(PSTR("TestPulse: Interrupt RXPIN: %d"), _rx_pin);
  pinMode(_rx_pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(_rx_pin), countInterrupt, FALLING);
#endif

  Log::console(PSTR("TestPulse: Test TXPIN: %d"), _tx_pin);
  if (TestTimer::tx_pin(_tx_pin, s_tx_mask, s_tx_hi)) {
    pinMode(_tx_pin, OUTPUT);
  } else if (_tx_pin >= 0) {
    Log::console(PSTR("TestPulse: TXPIN %d can't be driven, no TX"), _tx_pin);
  }
  CPMAdjuster();
  _next_delay = (uint32_t)calcDelay();
  _this_delay = _next_delay;
  TestTimer::start(pulseInterrupt, _next_delay);
}

void GeigerTestPulse::stopForOTA() { TestTimer::stop(); }

void GeigerTestPulse::restartAfterOTA() { TestTimer::start(pulseInterrupt, _next_delay); }

void GeigerTestPulse::loop() {
  countTimerPulses();
  if (_last_pulse_test != _last_b) {
    _last_pulse_test = _last_b;
    _next_delay = (uint32_t)calcDelay();   // one word, the isr reads it whole
  }
}

// returns ticks to the next
uint32_t IRAM_ATTR GeigerTestPulse::pulseInterrupt() {
  _bool_pulse_state = !_bool_pulse_state;
  TestTimer::tx(s_tx_mask, s_tx_hi, _bool_pulse_state);
  uint32_t _our_delay = 0;
  uint32_t pulseWidthTicks = _pulse_width_ticks;
  if (!_bool_pulse_state) {
#ifdef GEIGER_COUNT_TXPULSE
    GeigerInputTest::timerPulse();
#endif
    _last_b = micros();
    uint32_t next = _next_delay;
    _this_delay = (next > 2 * pulseWidthTicks) ? next - pulseWidthTicks : pulseWidthTicks;
    _our_delay = _this_delay;
  } else {
    _our_delay = pulseWidthTicks;
  }
  return _our_delay;
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
