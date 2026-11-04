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

#include "App/ExternalPause.h"
#include "Corium/Console/Console.h"
#include "Corium/Logger/Logger.h"
#include "Corium/System/FastMillis.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"

static const int32_t PAUSE_MAX_S = 86400;

static Deadline s_pause;

void ExternalPause::start(uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    s_pause.clear();
    Log::console(PSTR("External posts: resumed"));
    return;
  }
  s_pause.set(fast_millis() + timeout_ms);
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
  if (!s_pause.armed()) return false;
  if (reached_max(fast_millis(), s_pause.at, PAUSE_MAX_S * 1000U)) {
    s_pause.clear();
    Log::console(PSTR("External posts: resumed (timeout)"));
    return false;
  }
  return true;
}

// a query logs the end too
uint32_t ExternalPause::remaining_ms() {
  if (!active()) return 0;
  int32_t d = (int32_t)(s_pause.at - fast_millis());
  return d > 0 ? (uint32_t)d : 0;
}

// pause [s], 0 resumes
static void cmd_pause() {
  char* arg = Console::next();
  if (arg) {
    if (!ExternalPause::start_s(arg)) Log::console(PSTR("pause [seconds], 0 resumes"));
    return;
  }
  uint32_t rem = ExternalPause::remaining_ms();
  if (rem) Log::console(PSTR("External posts: paused %u s remaining"), (unsigned)(rem / 1000U));
  else     Log::console(PSTR("External posts: active"));
}
CORIUM_REGISTER_CMD(pause, cmd_pause)
