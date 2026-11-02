/*
  Clock.cpp - wall-clock time, set by whichever source syncs it.

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

#include "Clock.h"
#include <sys/time.h>
#include "Uptime.h"
#include "Corium/Logger/Logger.h"

namespace Clock {

bool synced = false;
Source source = NONE;
time_t boot_epoch = 0;
int16_t tz_offset_min = 0;
bool resync = false;
unsigned long resync_ms = 0;

static const unsigned long RESYNC_MS = 5UL * 60UL * 1000UL;
static StepFn s_on_step = nullptr;

void on_step(StepFn fn) { s_on_step = fn; }

void refresh_tz_offset_min() {
  time_t now = time(NULL);
  struct tm loc, utc;
  localtime_r(&now, &loc);
  gmtime_r(&now, &utc);
  int days = loc.tm_yday - utc.tm_yday;
  if (days > 1) days = -1;   // new year
  else if (days < -1) days = 1;
  tz_offset_min = (int16_t)((days * 24 + loc.tm_hour - utc.tm_hour) * 60 + loc.tm_min - utc.tm_min);
}

bool on_sync(Source s) {
  bool first = !synced;
  bool fresh = source != s;
  unsigned long uptime = Uptime::since(Uptime::start);
  if (first) {
    synced = true;
    boot_epoch = time(NULL) - uptime;
  } else {
    // drift moves it a little each sync, a bad earlier reply a lot, a wrong century 2^32 s and more
    time_t delta = time(NULL) - (boot_epoch + (time_t)uptime);
    boot_epoch += delta;
    if (delta > STEP_S || delta < -STEP_S) {
      Log::console(PSTR("Clock: stepped %ld s"), (long)delta);
      if (s_on_step) s_on_step((int32_t)delta);   // the same mod 2^32, all its 32-bit stamps need
    }
  }
  source = s;
  refresh_tz_offset_min();
  return fresh;
}

bool set(time_t epoch, Source s) {
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);
  return on_sync(s);
}

// a power-on starts at 0
void kept() {
  if (synced || time(NULL) < MIN_EPOCH) return;
  on_sync(KEPT);
  Log::console(PSTR("Clock: kept over the restart"));
}

bool resync_due(unsigned long now) {
  if (!resync) return false;
  if (resync_ms && now - resync_ms < RESYNC_MS) return false;
  resync = false;
  resync_ms = now;
  return true;
}

}
