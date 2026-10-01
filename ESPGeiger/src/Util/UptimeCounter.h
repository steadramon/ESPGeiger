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
// tick() must be called at least once per wrap or a whole wrap is lost. The
// unsigned delta is correct across the wrap. Do not scale or compare a
// reconstructed time: it overflows uint32 at 4294967 s, the instant millis()
// wraps.
//
// Types are uint32_t, not `unsigned long`: same width on target, 64-bit on a
// host, where a wrap test would silently never fire.
class UptimeCounter {
public:
  uint32_t tick(uint32_t now_ms) {
    if (now_ms < _last_ms) _wraps++;
    _acc += now_ms - _last_ms;
    _last_ms = now_ms;
    while (_acc >= 1000u) { _acc -= 1000u; _value++; }
    return _value;
  }

  // Result of the last tick(). tick() is a read-modify-write, so it needs a
  // single writer; every other reader takes this instead. Zero until the
  // first tick().
  uint32_t value() const { return _value; }

  // Diagnostic; tick() does not use it.
  uint16_t wraps() const { return _wraps; }

private:
  uint32_t _last_ms = 0;
  uint32_t _acc     = 0;   // ms not yet counted, always < 1000 after tick()
  uint32_t _value   = 0;
  uint16_t _wraps   = 0;
};

#endif
