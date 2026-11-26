/*
  GeigerInput/Type/TestTimer1.cpp - test pulse timer, EGTimer1 on esp8266


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
#include "TestTimer.h"
#include "GeigerInput/GeigerInputTest.h"
#include "Corium/Platform/Chip.h"

#if GEIGER_IS_TEST(GEIGER_TYPE)

#if CORIUM_HAS_TIMER1
#include <EGTimer1.h>

static uint32_t (*s_fn)();

// 26.8 s, fits EGTimer1 once << 8
static const uint32_t TT_MAX = (1u << 23) - 1;
static uint32_t s_wait;

// EGTimer1 counts cpu ticks
static uint32_t IRAM_ATTR arm(uint32_t t) {
  s_wait = t > TT_MAX ? t - TT_MAX : 0;
  return (t > TT_MAX ? TT_MAX : t) << GEIGER_TEST_TICK_SHIFT;
}

static uint32_t IRAM_ATTR on_nmi() { return arm(s_wait ? s_wait : s_fn()); }

void TestTimer::start(uint32_t (*fn)(), uint32_t first) {
  s_fn = fn;
  EGTimer1::timer_start(on_nmi, arm(first));
}

void TestTimer::stop() { EGTimer1::timer_stop(); }

void TestTimer::restart(uint32_t first) {
  EGTimer1::timer_stop();
  start(s_fn, first);
}

#endif

#endif
