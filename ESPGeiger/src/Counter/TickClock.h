/*
  TickClock.h - epoch seconds for the 1 s tick, counted between reads.

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

#ifndef ESPGEIGER_TICKCLOCK_H
#define ESPGEIGER_TICKCLOCK_H

#include <stdint.h>
#include <time.h>

// time() once a minute and up to the boundary, +1 a tick between;
// an ntp step lands within the minute
struct TickClock {
  static constexpr uint8_t READ_EVERY = 60;
  time_t  now = 0;
  uint8_t age = 0;

  // boundary 0 = not known yet, read is time()
  time_t step(time_t boundary, time_t (*read)(time_t*)) {
    if (now && ++age < READ_EVERY && now + 1 < boundary) return ++now;
    age = 0;
    now = read(nullptr);
    return now;
  }
};

#endif
