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
};

void GeigerTest::begin() {
  GeigerInput::begin();
  Log::console(PSTR("GeigerTest: Setting up test geiger ..."));
  setTargetCPM(30.0);
  double delay = calcDelay();
#ifdef ESP8266
  timer1_attachInterrupt(testInterrupt);
  timer1_isr_init();
  timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);
  timer1_write(delay);
#else
  pulsetimer = timerBegin(3, 80, true);
  timerAttachInterrupt(pulsetimer, &testInterrupt, true);
  timerAlarmWrite(pulsetimer, delay, true);
  timerAlarmEnable(pulsetimer);
#endif
}

double GeigerTest::calcDelay() {
#ifdef ESP8266
  double mult = 320000;
#else
  double mult = 1000000;
#endif
#ifdef DISABLE_GEIGER_POISSON
  return mult / _target_cps;
#else
  double result = generatePoissonRandom(_target_cps);
  return mult * result;
#endif
}

void GeigerTest::setTargetCPM(float target) {
  setTargetCPS(target/60.0);
};

void GeigerTest::setTargetCPS(float target) {
  _target_cps = target;
};

void GeigerTest::secondTicker() {
  CPMAdjuster();
}

void GeigerTest::testInterrupt() {
  GeigerInput::countInterrupt();
  _pulse_test_send = true;
}

void GeigerTest::loop() {
#ifdef DISABLE_GEIGER_POISSON
  return;
#endif
  if (_pulse_test_send == true) {
    double delay = calcDelay();
#ifdef ESP8266
    if (delay > 50) {
      timer1_write(delay);
    }
#else
    if (delay > 25) {
      timerAlarmWrite(pulsetimer, delay, true);
    }
#endif
    _pulse_test_send = false;
  }
}

void GeigerTest::CPMAdjuster() {
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
    Log::console(PSTR("GeigerTest: Setting CPM to: %d"), _targetCPM);
    setTargetCPM(_targetCPM);
  }
#endif
}
