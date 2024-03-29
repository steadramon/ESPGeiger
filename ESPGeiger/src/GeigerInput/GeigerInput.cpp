/*
  GeigerInput/GeigerInput.cpp - Geiger Counter Input Class
  
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
#include <Arduino.h>
#include "GeigerInput.h"
#include "../Logger/Logger.h"

void GeigerInput::begin() {

}

int GeigerInput::collect() {
#ifdef ESP32
    portENTER_CRITICAL_ISR(&timerMux);
#endif
  _eventFlipFlop = !_eventFlipFlop;
#ifdef ESP32
    portEXIT_CRITICAL_ISR(&timerMux);
#endif    
  int current = 0;
  if (_eventFlipFlop == false) {
    current = eventCounter1;
    eventCounter1 = 0;
  } else {
    current = eventCounter2;
    eventCounter2 = 0;
  }
  return current;
}

void GeigerInput::loop() {
}

void GeigerInput::secondTicker() {
}

void GeigerInput::setLastBlip()  {
  unsigned long cycles = ESP.getCycleCount();
  _last_blip = cycles;
}

unsigned long GeigerInput::last_blip()  {
  return _last_blip;
}

void GeigerInput::countInterrupt() {
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - _last_blip < _debounce) {
    return;
  }
#ifdef ESP32
  portENTER_CRITICAL_ISR(&timerMux);
#endif
  if (_eventFlipFlop == true)
    eventCounter1++;
  else
    eventCounter2++;
#ifdef ESP32
  portEXIT_CRITICAL_ISR(&timerMux);
#endif    
  _last_blip = cycles;
}

void GeigerInput::setCounter(int val) {
  setCounter(val, true);
}

void GeigerInput::setCounter(int val, bool update = true) {
  if (_eventFlipFlop == true)
    eventCounter1 = val;
  else
    eventCounter2 = val;
  if (!update)
    return;
  unsigned long cycles = ESP.getCycleCount();
  _last_blip = cycles;
}

double GeigerInput::generatePoissonRandom(double lambda) {
  // https://tomroelandts.com/articles/simulating-a-geiger-counter
	double u;
	u = random(RAND_MAX) * 1.0 / RAND_MAX;
	return -log(1 - u) / lambda;
}