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
#include "Globals.h"
#include "Uptime.h"
#include "../Logger/Logger.h"

namespace Clock {

bool synced = false;
unsigned long boot_epoch = 0;
int16_t tz_offset_min = 0;
unsigned long last_sync_ms = 0;
volatile bool resync = false;
unsigned long resync_ms = 0;

static const unsigned long RESYNC_MS = 5UL * 60UL * 1000UL;
static StepFn s_on_step = nullptr;

void on_step(StepFn fn) { s_on_step = fn; }

void refresh_tz_offset_min() {
  time_t now = time(NULL);
  struct tm gmt_tm;
  gmtime_r(&now, &gmt_tm);
  gmt_tm.tm_isdst = -1;
  tz_offset_min = (int16_t)((now - mktime(&gmt_tm)) / 60);
}

bool on_sync() {
  last_sync_ms = millis();
  bool first = !synced;
  unsigned long uptime = Uptime::seconds() - start;
  if (first) {
    synced = true;
    boot_epoch = epoch() - uptime;
  } else {
    // drift moves it a little each sync, a bad earlier reply a lot
    int32_t delta = (int32_t)(epoch() - (uint32_t)(boot_epoch + uptime));
    boot_epoch += delta;
    if (delta > STEP_S || delta < -STEP_S) {
      Log::console(PSTR("Clock: stepped %ld s"), (long)delta);
      if (s_on_step) s_on_step(delta);
    }
  }
  refresh_tz_offset_min();
  return first;
}

bool resync_due(unsigned long now) {
  if (!resync) return false;
  if (resync_ms && now - resync_ms < RESYNC_MS) return false;
  resync = false;
  resync_ms = now;
  return true;
}

}
