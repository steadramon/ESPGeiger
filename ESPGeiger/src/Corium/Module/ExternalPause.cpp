/*
  ExternalPause.cpp - see header.

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

#include "ExternalPause.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"

static const int32_t PAUSE_MAX_S = 86400;

static uint32_t s_pause_until_ms = 0;

void ExternalPause::start(uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    s_pause_until_ms = 0;
    Log::console(PSTR("External posts: resumed"));
    return;
  }
  uint32_t until = fast_millis() + timeout_ms;
  s_pause_until_ms = until ? until : 1;   // 0 = not paused
  Log::console(PSTR("External posts: paused for %u s"), (unsigned)(timeout_ms / 1000U));
}

// junk mustn't end a running pause
bool ExternalPause::start_s(const char* secs) {
  int32_t n;
  if (!parse_i32(secs, &n)) return false;
  start((uint32_t)clamp<int32_t>(n, 0, PAUSE_MAX_S) * 1000U);
  return true;
}

bool ExternalPause::active() {
  uint32_t until = s_pause_until_ms;
  if (until == 0) return false;
  if (reached(fast_millis(), until)) {
    s_pause_until_ms = 0;
    Log::console(PSTR("External posts: resumed (timeout)"));
    return false;
  }
  return true;
}

uint32_t ExternalPause::remaining_ms() {
  uint32_t until = s_pause_until_ms;
  if (until == 0) return 0;
  int32_t d = (int32_t)(until - fast_millis());
  return d > 0 ? (uint32_t)d : 0;
}
