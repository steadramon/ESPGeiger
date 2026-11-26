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
#include "RtcMap.h"
#include "Corium/Logger/Logger.h"
#include "Corium/Util/StringUtil.h"

namespace corium {
namespace Clock {

bool synced = false;
Source source = NONE;
time_t bootEpoch = 0;
int16_t tzOffsetMin = 0;
bool resync = false;
unsigned long resyncMs = 0;

static const unsigned long RESYNC_MS = 5UL * 60UL * 1000UL;
static StepFn s_on_step = nullptr;

void onStep(StepFn fn) { s_on_step = fn; }

// Local minus UTC, in minutes
void refreshTzOffsetMin() {
  time_t now = time(NULL);
  struct tm loc, utc;
  localtime_r(&now, &loc);
  gmtime_r(&now, &utc);
  int days = loc.tm_yday - utc.tm_yday;
  if (days > 1) days = -1;   // across new year
  else if (days < -1) days = 1;
  tzOffsetMin = (int16_t)((days * 24 + loc.tm_hour - utc.tm_hour) * 60 + loc.tm_min - utc.tm_min);
}

bool onSync(Source s) {
  bool first = !synced;
  bool changed = source != s;
  unsigned long uptime = Uptime::since(Uptime::start);
  if (first) {
    synced = true;
    bootEpoch = time(NULL) - uptime;
  } else {
    // Normal drift moves it a little, a bad earlier reply moves it a lot
    time_t delta = time(NULL) - (bootEpoch + (time_t)uptime);
    bootEpoch += delta;
    if (delta > STEP_S || delta < -STEP_S) {
      char s[24];
      fmtU64(s, sizeof(s), delta < 0 ? 0 - (uint64_t)delta : (uint64_t)delta);
      Log::console(PSTR("Clock: Stepped %s%s s"), delta < 0 ? "-" : "", s);
      if (s_on_step) s_on_step((int32_t)delta);   // mod 2^32 is all 32-bit stamps need
    }
  }
  source = s;
  refreshTzOffsetMin();
  return changed;
}

void set(time_t epoch, Source s) {
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);
  onSync(s);
}

// time() survives a restart in the IDF's rtc words, but a new build can move them
static constexpr uint32_t STAMP_MAGIC = 0xC10C0014u;
static constexpr uint32_t KEPT_GAP_S = 300;   // covers the reset and a stall before it
struct StampRtc { uint32_t magic; uint32_t epoch; };
CORIUM_RTC_SLOT(StampSlot, StampRtc, RtcMap::CLOCK_AT, RtcMap::CLOCK_WORDS);

// Worked out from the anchor, calling the esp32 time() every second doubled the tick
void stamp() {
  if (!synced) return;
  StampSlot::write(StampRtc{STAMP_MAGIC, (uint32_t)(bootEpoch + (time_t)Uptime::since(Uptime::start))});
}

// For a wait before the Reactor runs, uptime stands still then so it uses time()
void stampNow() {
  if (synced) StampSlot::write(StampRtc{STAMP_MAGIC, (uint32_t)time(NULL)});
}

// A power-on starts time() at 0, so this only keeps it over a restart
void kept() {
  if (synced || time(NULL) < BUILT_EPOCH) return;
  StampRtc s;
  if (!StampSlot::read(s) || s.magic != STAMP_MAGIC) return;
  if ((uint32_t)time(NULL) - s.epoch > KEPT_GAP_S) return;   // catches a time before the stamp too, mod 2^32
  onSync(KEPT);
  stamp();   // so a restart loop before the Reactor runs keeps it too
  Log::console(PSTR("Clock: Kept over restart"));
}

// The wifi join or portal ran since the clock was set, re-anchor without a step
void anchor() {
  if (synced) bootEpoch = time(NULL) - Uptime::since(Uptime::start);
}

bool resyncDue(unsigned long now) {
  if (!resync) return false;
  if (resyncMs && now - resyncMs < RESYNC_MS) return false;
  resync = false;
  resyncMs = now;
  return true;
}

}
}  // namespace corium
