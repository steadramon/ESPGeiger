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

// fn is isr, returns ticks to next, 0 stops
namespace TestTimer {
  void start(uint32_t (*fn)(), uint32_t first);
  void stop();
  // false: no fast write for this pin
  bool tx_pin(int pin, uint32_t& mask, bool& hi);

  // hi = pins 32 and up
  static inline __attribute__((always_inline)) void tx(uint32_t mask, bool hi, bool high) {
#ifdef ESP8266
    (void)hi;
    if (high) GPOS = mask;
    else      GPOC = mask;
#else
#ifdef GPIO_OUT1_W1TS_REG
    if (hi) {
      REG_WRITE(high ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG, mask);
      return;
    }
#else
    (void)hi;
#endif
    // C3 wraps out_w1ts in a union
    REG_WRITE(high ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, mask);
#endif
  }
}

#endif
