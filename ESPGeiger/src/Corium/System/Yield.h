/*
  Yield.h - a yield after a long piece of work

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
#ifndef CORIUM_YIELD_H
#define CORIUM_YIELD_H

#include <Arduino.h>
#ifdef ESP32
#include <esp_task_wdt.h>
#endif

namespace corium {

// The 8266 only yields once loop() has run for the given us, esp32 always yields
static inline void yieldAfter(uint32_t us) {
#ifdef ESP8266
  optimistic_yield(us);
#else
  (void)us;
  yield();
#endif
}

// For a long walk in loop(). yield() feeds the 8266 wdt, the esp32 task wdt needs its own feed
static inline void yieldWalk() {
  yield();
#ifdef ESP32
  esp_task_wdt_reset();
#endif
}

}  // namespace corium

#endif
