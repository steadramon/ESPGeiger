/*
  GeigerInputTest.h - Class for Test type counter
  
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
#ifndef GEIGERINPUTTEST_H
#define GEIGERINPUTTEST_H

#include <Arduino.h>
#include "GeigerInput.h"

#ifndef GEIGER_TESTPULSE_ADJUSTTIME
#define GEIGER_TESTPULSE_ADJUSTTIME 300000
#endif

#ifndef GEIGERTESTMODE
#define GEIGERTESTMODE
#endif

#ifdef ESP8266
#define GEIGER_TEST_TIMER_FREQ TIM_DIV256
#define GEIGER_TEST_TIMER_DIV 312500
#else
#define GEIGER_TEST_TIMER_FREQ 80
#define GEIGER_TEST_TIMER_DIV 1000000
#endif

//#define GEIGER_TEST_FAST
//#define DISABLE_GEIGER_POISSON

class GeigerInputTest : public GeigerInput
{
  public:
    GeigerInputTest();
    virtual void begin();
    virtual void loop();
    virtual void secondTicker();
    void setTargetCPM(float target, bool manual);
    void setTargetCPS(float target);
    float getTargetCPS();
    void CPMAdjuster();
    double calcDelay();
    char _test_type[16];
    unsigned long _last_pulse_test = 0;
    double _remainder = 0;
    long getRandom();
    double generatePoissonRand(double lambda);
  private:
    float _target_cps = 0;
    int _current_selection = -1;
    bool _manual = false;
};
#endif