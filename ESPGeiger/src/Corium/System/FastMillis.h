/*
  FastMillis.h - Cheap millisecond counter driven by the ms ticker.

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

#ifndef CORIUM_FASTMILLIS_H
#define CORIUM_FASTMILLIS_H

#include <stdint.h>

// Cheaper than millis(), not for sub-ms

// A test build starts it near the wrap
#ifndef CORIUM_MS_START
#define CORIUM_MS_START 0
#endif

namespace corium {
extern volatile uint32_t _fast_ms_counter;

static inline uint32_t fastMillis() { return _fast_ms_counter; }

// true when now is at or past at. Wrap-safe, any unit
static inline bool reached(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }

// Reached, or more than max away. Without max, an at left 24.8 d behind would read as ahead
static inline bool reachedMax(uint32_t now, uint32_t at, uint32_t max) {
  return reached(now, at) || at - now > max;
}

// at or past, then sets the next one ms from now
static inline bool dueEvery(uint32_t now, uint32_t& at, uint32_t ms) {
  if (!reached(now, at)) return false;
  at = now + ms;
  return true;
}

// ms or more since last. last moves on in whole ms steps so the phase holds
static inline bool dueSteps(uint32_t now, uint32_t& last, uint32_t ms) {
  uint32_t gone = now - last;
  if (gone < ms) return false;
  last += ms ? gone / ms * ms : gone;   // 0 = each call
  return true;
}

// Stamped less than ms ago, 0 = never
static inline bool fresh(uint32_t now, uint32_t last, uint32_t ms) {
  return last != 0 && now - last < ms;
}

// 0 = off, so a time landing on 0 moves to 1
struct Deadline {
  uint32_t at = 0;
  void set(uint32_t t) { at = t ? t : 1; }
  void clear() { at = 0; }
  bool armed() const { return at != 0; }
  bool due(uint32_t now) const { return at != 0 && reached(now, at); }
};

// Once stale it stays stale until the next mark, so an old stamp never comes back on the wrap
struct Stamp {
  uint32_t at = 0;   // 0 = never
  void mark(uint32_t now) { at = now ? now : now - 1; }
  void clear() { at = 0; }
  bool fresh(uint32_t now, uint32_t ms) {
    if (at && now - at >= ms) at = 0;
    return at != 0;
  }
};
}  // namespace corium

#endif
