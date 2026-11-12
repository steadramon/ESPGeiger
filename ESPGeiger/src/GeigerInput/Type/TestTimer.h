/*
  GeigerInput/Type/TestTimer.h - test pulse timer, EGTimer1 on esp8266, esp_timer on esp32

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
#ifndef TESTTIMER_H
#define TESTTIMER_H

#include <Arduino.h>

// fn runs in the isr and returns ticks to the next, 0 stops
namespace TestTimer {
  void start(uint32_t (*fn)(), uint32_t first);
  void stop();
  void restart(uint32_t first);
}

#endif
