/*
  TickProfile.h - sTickerCB timing + loop iteration counter.

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
#ifndef UTIL_TICKPROFILE_H
#define UTIL_TICKPROFILE_H

#include <Arduino.h>

namespace TickProfile {
  extern uint32_t tick_us;      // sTickerCB duration, EMA-smoothed (alpha = 1/8)
  extern uint32_t tick_max_us;  // peak tick_us since last 60s window reset
  extern uint32_t lps;          // loop() iterations counted in last second
  extern volatile uint32_t _lps_count;

  inline void countIter() { ++_lps_count; }

  void beginTick();
#ifdef TICK_PROFILE
  void markCounter();
  void markWifi();
  void markModules();
#endif
  void endTick();
}

#endif
