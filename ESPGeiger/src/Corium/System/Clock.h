/*
  Clock.h - wall-clock time, set by whichever source syncs it.

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

#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>
#include <time.h>
#include "Corium/System/FastMillis.h"

namespace Clock {
  extern bool synced;
  extern unsigned long boot_epoch;

  // Cached, localtime is slow
  extern int16_t tz_offset_min;
  void refresh_tz_offset_min();

  bool on_sync();   // true on first sync

  // a sync off by more than this tells on_step
  static const int32_t STEP_S = 60;
  using StepFn = void (*)(int32_t delta_s);
  void on_step(StepFn fn);

  // a server says the clock is off, resync at most every 5 min
  extern bool resync;
  extern unsigned long resync_ms;
  bool resync_due(unsigned long now);

  // 32 bits, time_t is 64 on some cores; good to 2106
  inline uint32_t epoch() { return (uint32_t)time(NULL); }

  // 2001, below it = never set
  static const uint32_t MIN_EPOCH = 1000000000UL;

  inline bool utcTm(struct tm* out) {
    if (!synced) return false;
    time_t now = time(NULL);
    gmtime_r(&now, out);
    return true;
  }

  inline bool localTm(struct tm* out) {
    if (!synced) return false;
    time_t local = time(NULL) + (time_t)tz_offset_min * 60;
    gmtime_r(&local, out);
    return true;
  }
}

#endif
