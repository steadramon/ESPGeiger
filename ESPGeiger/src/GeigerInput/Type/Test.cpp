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
  setTargetCPM(123.45);
#ifdef ESP8266
  timer1_attachInterrupt(countInterrupt);
  timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);
  timer1_write(_target_pwm);
#else
  pulsetimer = timerBegin(3, 80, true);
  timerAttachInterrupt(pulsetimer, &countInterrupt, true);
  timerAlarmWrite(pulsetimer, _target_pwm, true);
  timerAlarmEnable(pulsetimer);
#endif
}

void GeigerTest::setTargetCPM(float target) {
  setTargetCPS(target/60.0);
};

void GeigerTest::setTargetCPS(float target) {
#ifdef ESP8266
  _target_pwm = (int)320000/target;
#else
  _target_pwm = (int)1000000/target;
#endif
};

void GeigerTest::secondTicker() {
  CPMAdjuster();
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
#ifdef ESP8266
    timer1_write(_target_pwm);
#else
    timerAlarmWrite(pulsetimer, _target_pwm, true);
#endif
  }
#endif
}
