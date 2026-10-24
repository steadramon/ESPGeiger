/*
  Reactor.cpp - the 1 s tick: a Ticker on esp8266, due in the loop on esp32.

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
#include "Reactor.h"

#ifdef ESP32
uint32_t Reactor::s_next_sec = 0;

void Reactor::begin(void (*)()) {
  s_next_sec = fast_millis() + 1000;
  // the tick in loop feeds it
  esp_task_wdt_add(NULL);
}
#else
#include <Ticker.h>

static Ticker sTicker;

void Reactor::begin(void (*second)()) {
  sTicker.attach(1, second);
}
#endif
