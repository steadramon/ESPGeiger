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
  _handlesecond = true;
  int current = eventCounter;
#ifdef ESP32
    portENTER_CRITICAL_ISR(&timerMux);
#endif
    eventCounter = 0;
#ifdef ESP32
    portEXIT_CRITICAL_ISR(&timerMux);
#endif    
  _handlesecond = false;
  return current;
}

void GeigerInput::loop() {
}

void GeigerInput::secondticker() {
}

void GeigerInput::setLastBlip()  {
  unsigned long cycles = ESP.getCycleCount();
  _last_blip = cycles;
}

unsigned long GeigerInput::last_blip()  {
  return _last_blip;
}

void GeigerInput::countInterrupt() {
  if (_handlesecond)
    return;
  unsigned long cycles = ESP.getCycleCount();
  if (cycles - _last_blip > _debounce) {
#ifdef ESP32
    portENTER_CRITICAL_ISR(&timerMux);
#endif
    eventCounter++;
#ifdef ESP32
    portEXIT_CRITICAL_ISR(&timerMux);
#endif    
    _last_blip = cycles;
  }
}

void GeigerInput::setCounter(int val) {
  setCounter(val, true);
}

void GeigerInput::setCounter(int val, bool update = true) {
  if (_handlesecond)
    return;
  eventCounter = val;
  if (!update)
    return;
  unsigned long cycles = ESP.getCycleCount();
  _last_blip = cycles;
}
