/*
  GeigerInput/GeigerInputTest.cpp - Base class for test-type Geiger inputs

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
#include "GeigerInputTest.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/System/FastMillis.h"
#include <EGHttpServer.h>

void GeigerInputTest::appendClicksExtra(EGHttpResponse& res) {
  res.sendChunk(F(",\"roll\":60"));
}

volatile int _pulse_width_us = GEIGER_PULSE_WIDTH;
volatile uint32_t _pulse_width_ticks =
    (uint32_t)((double)GEIGER_PULSE_WIDTH * GEIGER_TEST_TIMER_DIV / 1000000);

void set_pulse_width_us(int us) {
  _pulse_width_us = us;
  _pulse_width_ticks = (uint32_t)((double)us * GEIGER_TEST_TIMER_DIV / 1000000);
}

GeigerInputTest::GeigerInputTest() {
};

// one writer each side, cross core on esp32
static constexpr uint8_t TP_RING = 64;
static volatile uint32_t s_tp_at[TP_RING];
volatile uint8_t GeigerInputTest::_tp_head;
volatile uint8_t GeigerInputTest::_tp_tail;

void IRAM_ATTR GeigerInputTest::timerPulse() {
  uint8_t h = _tp_head;
  if ((uint8_t)(h - _tp_tail) >= TP_RING) return;   // full, dropped
  s_tp_at[h % TP_RING] = micros();
  _tp_head = h + 1;
}

void GeigerInputTest::drainTimerPulses() {
  uint8_t t = _tp_tail;
  while (t != _tp_head) {
    uint32_t at = s_tp_at[t % TP_RING];
    noInterrupts();   // a pin interrupt may count too
    countAt(at);
    interrupts();
    _tp_tail = ++t;
  }
}

int GeigerInputTest::collect() {
  countTimerPulses();
  return GeigerInput::collect();
}

void GeigerInputTest::begin() {
  Log::console(PSTR("%s: Setting up test geiger ..."), _test_type);
  GeigerInput::begin();
}

double GeigerInputTest::generatePoissonRand(double lambda) {
  // https://tomroelandts.com/articles/simulating-a-geiger-counter
  double u;
  u = (double)DeviceInfo::hwRandom() / ((double)0xFFFFFFFF + 1);
  return -log(1 - u) / lambda;
}

double GeigerInputTest::calcDelay() {
  double mult = GEIGER_TEST_TIMER_DIV;
  double cps  = getTargetCPS();
  double r;
#ifdef DISABLE_GEIGER_POISSON
  r = mult / cps;
#else
  // debt unclamped, keeps it Poisson
  double interval     = generatePoissonRand(cps) + _remainder;
  double min_interval = (double)_pulse_width_us / 1000000;
  if (interval < min_interval) {
    _remainder = interval - min_interval;
    interval   = min_interval;
  } else {
    _remainder = 0;
  }
  r = mult * interval;
#endif
  if (r > UINT32_MAX) r = UINT32_MAX;
  return r;
}

void GeigerInputTest::setTargetCPM(float target, bool manual = false) {
  int c = (int)(target * 100 + 0.5f);
  Log::console(PSTR("%s: Setting CPM to: %d.%02d"), _test_type, c / 100, c % 100);
  _manual = manual;
  setTargetCPS(target/60.0f);
};

void GeigerInputTest::setTargetCPS(float target) {
  _target_cps = target;
};

float GeigerInputTest::getTargetCPS() {
  // never 0, callers divide
  return (_target_cps > 0) ? _target_cps : 1e-6f;
}

void GeigerInputTest::secondTicker() {
  CPMAdjuster();
}

void GeigerInputTest::loop() {
}

void GeigerInputTest::CPMAdjuster() {
  if (_manual) {
    return;
  }
#ifndef GEIGER_TEST_FIXEDCPM
  int selection = (fast_millis() / GEIGER_TESTPULSE_ADJUSTTIME) % 4;
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
    setTargetCPM(_targetCPM, false);
  }
#endif
}
