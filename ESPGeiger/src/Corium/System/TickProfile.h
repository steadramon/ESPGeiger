/*
  TickProfile.h - Reactor::second timing + loop iteration counter.

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
#ifndef CORIUM_TICKPROFILE_H
#define CORIUM_TICKPROFILE_H

#include <Arduino.h>

namespace corium {
// loop and the tick only, esp32 runs the tick on loop too: no volatile
namespace TickProfile {
  extern uint32_t tickUs;
  extern uint32_t tickMaxUs;  // 60s
  extern uint32_t lps;
  extern uint32_t lpsRaw;
  extern uint32_t _lps_count;    // free-running, never reset

  inline void countIter() { ++_lps_count; }

#ifdef LOOP_PROFILE
  extern uint32_t loopBodyUs;
  inline void addLoopBody(uint32_t us) { loopBodyUs += us; }
#endif

  void beginTick();
#ifdef TICK_PROFILE
  void markApp();
  void markWifi();
#endif
  bool endTick();   // true once a minute
}
}  // namespace corium

#endif
