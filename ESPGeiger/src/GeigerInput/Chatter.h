/*
  Chatter.h - A ringing tube: the isr throws away much of what it sees, at a rate a real tube doesn't.

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
#ifndef CHATTER_H
#define CHATTER_H

#include <stdint.h>

// 10 s windows: on after 3 bad in a row, off after 6 good
struct Chatter {
  static constexpr uint8_t WIN_S = 10;
  static constexpr uint8_t ON_N = 3;
  static constexpr uint8_t OFF_N = 6;
  // under it a real tube's pulses inside the debounce stay rare (~2% at 200 us)
  static constexpr uint32_t MAX_CPS = 100;
  static constexpr uint32_t MIN_REJECTS = 10;   // a window, one bounce isn't a tube

  uint32_t seen = 0;
  uint32_t kept = 0;
  uint8_t secs = 0;
  uint8_t bad = 0;
  uint8_t good = 0;
  bool on = false;

  // a second's isr entries and the pulses kept of them; true = on changed
  bool second(uint32_t entries, uint32_t counted) {
    seen += entries;
    kept += counted;
    if (++secs < WIN_S) return false;
    return window();
  }

  // once a window, kept out of the second
  __attribute__((noinline)) bool window() {
    uint32_t rej = seen > kept ? seen - kept : 0;
    bool hot = kept > MAX_CPS * WIN_S;
    bool ring = rej >= MIN_REJECTS && rej * 4 >= seen;   // a quarter or more thrown away
    seen = 0;
    kept = 0;
    secs = 0;
    if (hot) return false;   // no say, keeps its state
    if (ring) {
      good = 0;
      if (bad < ON_N) bad++;
      if (on || bad < ON_N) return false;
      on = true;
      return true;
    }
    bad = 0;
    if (good < OFF_N) good++;
    if (!on || good < OFF_N) return false;
    on = false;
    return true;
  }
};

#endif
