/*
  TokenBucket.h - A rate cap: one token an interval, a few banked.

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

#ifndef CORIUM_TOKENBUCKET_H
#define CORIUM_TOKENBUCKET_H

#include <stdint.h>

// Adds whole intervals only, so the slack is kept. With restart, a full bucket starts the clock again
static inline bool takeToken(uint32_t now, uint32_t& last, uint8_t& tokens,
                              uint32_t every, uint8_t max, bool restart = false) {
  if (!every) return true;   // 0 = no cap
  uint32_t elapsed = now - last;
  if (elapsed >= every) {
    uint32_t gained = elapsed < 2u * every ? 1u : elapsed / every;
    tokens = gained >= (uint32_t)(max - tokens) ? max : (uint8_t)(tokens + gained);
    last += gained * every;
  }
  if (restart && tokens >= max) last = now;   // full, so no credit is banked
  if (tokens == 0) return false;
  tokens--;
  return true;
}

#endif
