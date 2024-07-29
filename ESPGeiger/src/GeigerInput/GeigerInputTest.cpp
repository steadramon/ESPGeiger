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
#include "GeigerInputTest.h"
#include "../Logger/Logger.h"

GeigerInputTest::GeigerInputTest() {
};

void GeigerInputTest::begin() {
  Log::console(PSTR("%s: Setting up test geiger ..."), _test_type);
#ifdef ESP8266
  randomSeed(RANDOM_REG32);
#else
  randomSeed(esp_random());
#endif
  GeigerInput::begin();
}

double GeigerInputTest::generatePoissonRand(double lambda) {
  // https://tomroelandts.com/articles/simulating-a-geiger-counter
	double u;
  u = random(RAND_MAX) * 1.0 / RAND_MAX;
	return -log(1 - u) / lambda;
}

double GeigerInputTest::calcDelay() {
  double mult = GEIGER_TEST_TIMER_DIV;
#ifdef DISABLE_GEIGER_POISSON
  return mult / _target_cps;
#else
  double result = generatePoissonRand(_target_cps);
  result += _remainder;
  _remainder = 0;

  double test_debounce = (_debounce*0.000001);
  if (result < test_debounce) {
    _remainder = result - test_debounce;
    result = test_debounce;
  }
  return (mult) * result;
#endif
}

void GeigerInputTest::setTargetCPM(float target, bool manual = false) {
  Log::console(PSTR("%s: Setting CPM to: %d"), _test_type, (int)target);
  _manual = manual;
  setTargetCPS(target/60.0);
};

void GeigerInputTest::setTargetCPS(float target) {
  _target_cps = target;
};

float GeigerInputTest::getTargetCPS() {
  return _target_cps;
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
    setTargetCPM(_targetCPM, false);
  }
#endif
}
