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

#ifdef ESP32
int GeigerTest::collect() {
  int current = timerRead(hwtimer);
  timerRestart(hwtimer);
  setCounter(current);
  return current;
}
#endif

void GeigerTest::begin() {
  GeigerInput::begin();
  Log::console(PSTR("GeigerTest: Setting up test geiger ..."));
#ifdef ESP8266
  timer1_attachInterrupt(countInterrupt);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
#ifdef GEIGER_TEST_FAST
  // 1666.66 CPS / 100000 CPM
  timer1_write(3000);
#else
  // 1 CPS / 60 CPM
  //timer1_write(5000000);
  // 1.66 CPS / 100 CPM
  timer1_write(3000000);
#endif
#else
#ifdef GEIGER_TEST_FAST
  hwtimer = timerBegin(3, 80, true);
#else
  hwtimer = timerBegin(3, 80000000, true);
#endif
  timerAlarmEnable(hwtimer);
#endif
}