/*
  Reactor.cpp - the 1 s ticker, run from the loop on esp32.

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
#include <Ticker.h>

static Ticker sTicker;

#ifdef ESP32
volatile uint8_t Reactor::s_ticks = 0;
uint8_t Reactor::s_ticks_run = 0;
static void sTickerPost() { Reactor::s_ticks++; }

void Reactor::begin(void (*)()) {
  sTicker.attach(1, sTickerPost);
  // the tick in loop feeds it
  esp_task_wdt_add(NULL);
}
#else
void Reactor::begin(void (*second)()) {
  sTicker.attach(1, second);
}
#endif
