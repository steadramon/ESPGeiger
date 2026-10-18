/*
  Uptime.h - the device's seconds since boot.

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

#ifndef ESPGEIGER_UPTIME_H
#define ESPGEIGER_UPTIME_H

#include <Arduino.h>
#include <time.h>
#include "UptimeCounter.h"

#ifndef SECS_PER_MIN
constexpr time_t SECS_PER_MIN  = 60UL;
constexpr time_t SECS_PER_HOUR = 3600UL;
constexpr time_t SECS_PER_DAY  = SECS_PER_HOUR * 24UL;
#endif

// tick() from the 1 Hz ticker only
namespace Uptime {
  extern UptimeCounter counter;
  inline time_t seconds() { return counter.value(); }
  inline time_t tick() { return counter.tick((uint32_t)::millis()); }

  struct Split { uint16_t d; uint8_t h, m, s; };
  // 32 bit maths, time_t is 64 on some cores
  inline Split split(uint32_t t) {
    const uint32_t day = SECS_PER_DAY, hour = SECS_PER_HOUR, min = SECS_PER_MIN;
    return { (uint16_t)(t / day), (uint8_t)(t % day / hour),
             (uint8_t)(t % hour / min), (uint8_t)(t % min) };
  }
}

#endif
