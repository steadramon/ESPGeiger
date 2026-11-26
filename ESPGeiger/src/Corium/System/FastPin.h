/*
  FastPin.h - an output pin set or cleared by register, isr safe

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
#ifndef CORIUM_FASTPIN_H
#define CORIUM_FASTPIN_H

#include <Arduino.h>
#include "Corium/System/PinSafety.h"

namespace corium {
namespace FastPin {

  // false = this pin can't be written by register
  inline bool bits(int pin, uint32_t& mask, bool& hi) {
    mask = 0;
    hi = false;
#ifdef ESP8266
    if (pin < 0 || pin > 15) return false;   // 16 is not on GPOS
#elif defined(GPIO_OUT1_W1TS_REG)
    if (pin < 0 || pin > MAX_GPIO_PIN) return false;
#else
    if (pin < 0 || pin > 31) return false;
#endif
    hi = pin >= 32;
    mask = (uint32_t)1 << (pin & 31);
    return true;
  }

  // hi = pins 32 and up
  static inline __attribute__((always_inline)) void write(uint32_t mask, bool hi, bool high) {
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
}  // namespace corium

#endif
