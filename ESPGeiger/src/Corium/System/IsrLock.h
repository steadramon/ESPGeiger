/*
  IsrLock.h - Short lock against an interrupt, one shape for both chips.

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
#ifndef ISRLOCK_H
#define ISRLOCK_H

#include <Arduino.h>

namespace IsrLock {

  // esp32 isr may be on the other core, 8266 just masks
  struct Mux {
#ifdef ESP32
    portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
#endif
  };

  // from the loop
  static inline __attribute__((always_inline)) void enter(Mux& x) {
#ifdef ESP32
    portENTER_CRITICAL(&x.m);
#else
    (void)x;
    noInterrupts();
#endif
  }

  static inline __attribute__((always_inline)) void leave(Mux& x) {
#ifdef ESP32
    portEXIT_CRITICAL(&x.m);
#else
    (void)x;
    interrupts();
#endif
  }

  // in the isr, 8266 isrs don't nest
  static inline __attribute__((always_inline)) void enter_isr(Mux& x) {
#ifdef ESP32
    portENTER_CRITICAL_ISR(&x.m);
#else
    (void)x;
#endif
  }

  static inline __attribute__((always_inline)) void leave_isr(Mux& x) {
#ifdef ESP32
    portEXIT_CRITICAL_ISR(&x.m);
#else
    (void)x;
#endif
  }

}

#endif
