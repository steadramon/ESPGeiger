/*
  ExternalPause.cpp - pause on external posters

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
#include "Corium/System/Uptime.h"
#include "Corium/Util/MathUtil.h"
#include "Corium/Util/StringUtil.h"

static const int32_t PAUSE_MAX_S = 86400;

static corium::Deadline s_pause;   // uptime s, no wrap

static uint32_t up_s() { return (uint32_t)corium::Uptime::seconds(); }

void ExternalPause::start(uint32_t secs) {
  if (secs == 0) {
    s_pause.clear();
    corium::Log::console(PSTR("External posts: Resumed"));
    return;
  }
  s_pause.set(up_s() + secs);
  corium::Log::console(PSTR("External posts: Paused for %u s"), (unsigned)secs);
}

// Bad input is refused here so it can't end a running pause
bool ExternalPause::start_s(const char* secs) {
  int32_t n;
  if (!parseI32(secs, &n)) return false;
  start((uint32_t)clamp<int32_t>(n, 0, PAUSE_MAX_S));
  return true;
}

bool ExternalPause::active() {
  if (!s_pause.armed()) return false;
  if (!s_pause.due(up_s())) return true;
  s_pause.clear();
  corium::Log::console(PSTR("External posts: Resumed (timeout)"));
  return false;
}

// Goes through active(), so asking also logs the end of an expired pause
uint32_t ExternalPause::remaining_s() {
  return active() ? s_pause.at - up_s() : 0;
}

// pause [s], 0 resumes
static void cmd_pause() {
  char* arg = corium::Console::next();
  if (arg) {
    if (!ExternalPause::start_s(arg)) corium::Log::console(PSTR("pause [seconds], 0 resumes"));
    return;
  }
  uint32_t rem = ExternalPause::remaining_s();
  if (rem) corium::Log::console(PSTR("External posts: Paused %u s remaining"), (unsigned)rem);
  else     corium::Log::console(PSTR("External posts: Active"));
}
CORIUM_REGISTER_CMD(pause, cmd_pause)
