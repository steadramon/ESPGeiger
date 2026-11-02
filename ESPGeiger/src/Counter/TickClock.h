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

// time() once a minute and on the boundary, +1 a tick between;
// an ntp step lands within the minute
struct TickClock {
  static constexpr uint8_t READ_EVERY = 60;
  uint32_t now = 0;
  uint8_t  left = 0;   // ticks counted before the next read, 0 = read

  // false = read due
  bool count() {
    if (!left) return false;
    left--;
    now++;
    return true;
  }

  // after the boundary moved
  void read(uint32_t t, uint32_t boundary) {
    now = t;
    uint32_t to = (int32_t)(boundary - t) > 0 ? boundary - t : 1;   // over the 2106 wrap too
    left = to < READ_EVERY ? (uint8_t)(to - 1) : READ_EVERY - 1;
  }

  void read_next() { left = 0; }
};

#endif
