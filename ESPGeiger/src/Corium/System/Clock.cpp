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

// local minus UTC, minutes
void refreshTzOffsetMin() {
  time_t now = time(NULL);
  struct tm loc, utc;
  localtime_r(&now, &loc);
  gmtime_r(&now, &utc);
  int days = loc.tm_yday - utc.tm_yday;
  if (days > 1) days = -1;   // new year
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
    // drift moves it a little each sync, a bad earlier reply a lot, a wrong century 2^32 s and more
    time_t delta = time(NULL) - (bootEpoch + (time_t)uptime);
    bootEpoch += delta;
    if (delta > STEP_S || delta < -STEP_S) {
      char s[24];
      fmtU64(s, sizeof(s), delta < 0 ? 0 - (uint64_t)delta : (uint64_t)delta);
      Log::console(PSTR("Clock: Stepped %s%s s"), delta < 0 ? "-" : "", s);
      if (s_on_step) s_on_step((int32_t)delta);   // the same mod 2^32, all its 32-bit stamps need
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

// time() over a restart is the IDF's rtc words: a new build can move them, garbage then
static constexpr uint32_t STAMP_MAGIC = 0xC10C0014u;
static constexpr uint32_t KEPT_GAP_S = 300;   // the reset, a stall before it
struct StampRtc { uint32_t magic; uint32_t epoch; };
CORIUM_RTC_SLOT(StampSlot, StampRtc, RtcMap::CLOCK_AT, RtcMap::CLOCK_WORDS);

// from the anchor, an esp32 time() a second doubled the tick; unsynced is 1970 or garbage
void stamp() {
  if (!synced) return;
  StampSlot::write(StampRtc{STAMP_MAGIC, (uint32_t)(bootEpoch + (time_t)Uptime::since(Uptime::start))});
}

// a wait before the Reactor, uptime stands still
void stampNow() {
  if (synced) StampSlot::write(StampRtc{STAMP_MAGIC, (uint32_t)time(NULL)});
}

// a power-on starts at 0
void kept() {
  if (synced || time(NULL) < BUILT_EPOCH) return;
  StampRtc s;
  if (!StampSlot::read(s) || s.magic != STAMP_MAGIC) return;
  if ((uint32_t)time(NULL) - s.epoch > KEPT_GAP_S) return;   // before it too, mod 2^32
  onSync(KEPT);
  stamp();   // a restart loop before the Reactor runs keeps it too
  Log::console(PSTR("Clock: Kept over restart"));
}

// the wifi join or portal ran since, no step
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
