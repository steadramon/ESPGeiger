/*
  UptimeCounter.h - seconds since boot from a wrapping 32-bit millis().

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

#ifndef CORIUM_UPTIMECOUNTER_H
#define CORIUM_UPTIMECOUNTER_H

#include <stdint.h>

namespace corium {
// seconds since boot across the 49.7 day millis() wrap, never value() * 1000
// uint32_t not unsigned long, a host build's 64 bits never wraps
class UptimeCounter {
public:
  uint32_t tick(uint32_t now_ms) {   // at least once per millis wrap
    if (now_ms < _last_ms) _wraps++;
    _acc += now_ms - _last_ms;
    _last_ms = now_ms;
    while (_acc >= 1000u) { _acc -= 1000u; _value++; }
    return _value;
  }

  // last tick()'s, 0 till the first; tick() has one writer, the rest read this
  uint32_t value() const { return _value; }

  // diagnostic only
  uint16_t wraps() const { return _wraps; }

private:
  uint32_t _last_ms = 0;
  uint32_t _acc     = 0;   // ms not yet counted, always < 1000 after tick()
  uint32_t _value   = 0;
  uint16_t _wraps   = 0;
};
}  // namespace corium

#endif
