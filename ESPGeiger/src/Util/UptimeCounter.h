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

#ifndef ESPGEIGER_UPTIMECOUNTER_H
#define ESPGEIGER_UPTIMECOUNTER_H

#include <stdint.h>

// Monotonic seconds since boot across the 49.71 day millis() wrap.
//
// tick() must be called at least once per wrap or the wrap is missed and the
// count stays 49.71 days low. Compare RAW millis only: comparing a
// reconstructed time overflows uint32 at 4294967 s, the same instant millis()
// wraps, so the check fails exactly when it is needed.
//
// Types are uint32_t, not `unsigned long`: same width on target, 64-bit on a
// host, where a wrap test would silently never fire.
//
// A wrap weighs 4294967 s, not 4294967.296, so the count runs ~0.3 s low per
// wrap (22 s per decade). Never steps backwards.
class UptimeCounter {
public:
  uint32_t tick(uint32_t now_ms) {
    if (now_ms < _last_ms) _wraps++;
    _last_ms = now_ms;
    return now_ms / 1000UL + (uint32_t)_wraps * 4294967UL;
  }

  uint16_t wraps() const { return _wraps; }

private:
  uint32_t _last_ms = 0;
  uint16_t _wraps   = 0;
};

#endif
