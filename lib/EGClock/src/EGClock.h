/*
  EGClock.h - One millis source for the EG libs. Set once, read by all.

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
#ifndef EGCLOCK_H
#define EGCLOCK_H

#include <stdint.h>

namespace EGClock {
using MillisFn = uint32_t (*)();
extern MillisFn now;    // EGClock::now(), millis() till set
void set(MillisFn fn);  // in setup(), before wifi; nullptr = millis()
}

#endif
