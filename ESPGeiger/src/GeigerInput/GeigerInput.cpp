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

volatile bool _eventFlipFlop = false;
volatile unsigned long _last_blip = 0;
volatile uint32_t eventCounter1 = 0;
volatile uint32_t eventCounter2 = 0;

volatile unsigned long _debounce = GEIGER_DEBOUNCE;

#ifdef ESP32
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

void GeigerInput::begin() {

}

uint32_t GeigerInput::collect() {
#ifdef ESP32
    portENTER_CRITICAL_ISR(&timerMux);
#endif
  _eventFlipFlop = !_eventFlipFlop;
#ifdef ESP32
    portEXIT_CRITICAL_ISR(&timerMux);
#endif
  uint32_t current = 0;
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
  _last_blip = micros();
}

void IRAM_ATTR GeigerInput::countInterrupt() {
  unsigned long cycles = micros();
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

void GeigerInput::setCounter(uint32_t val, bool update) {
  if (update) {
#ifdef ESP32
    portENTER_CRITICAL(&timerMux);
#else
    noInterrupts();
#endif
  }
  if (_eventFlipFlop == true)
    eventCounter1 = val;
  else
    eventCounter2 = val;
  if (update) {
#ifdef ESP32
    portEXIT_CRITICAL(&timerMux);
#else
    interrupts();
#endif
    _last_blip = micros();
  }
}
