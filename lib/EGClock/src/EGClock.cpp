/*
  EGClock.cpp - one millis source

  Copyright (C) 2026 @steadramon

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
#include "EGClock.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <chrono>
#endif

static uint32_t sys_ms() {
#ifdef ARDUINO
  return millis();
#else
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

EGClock::MillisFn EGClock::now = sys_ms;

void EGClock::set(MillisFn fn) {
  now = fn ? fn : sys_ms;
}
