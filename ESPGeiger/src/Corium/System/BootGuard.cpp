/*
  BootGuard.cpp - safe mode after crashes

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
#include "BootGuard.h"
#include "BootGuardState.h"
#include "CrashDump.h"
#include "DeviceInfo.h"
#include <stdint.h>
#include "Corium/Platform/Flash.h"

#ifdef ESP8266
#include "RtcMap.h"
#else
#include <esp_attr.h>
#endif

namespace corium {
namespace BootGuard {

static constexpr uint32_t MAGIC = 0xE56B0014;

#ifdef ESP8266
typedef RtcSlot<State, RtcMap::BOOTGUARD_AT, RtcMap::BOOTGUARD_WORDS> Slot;
static State s_state;

static void load() {
  if (!Slot::read(s_state)) s_state.magic = 0;
}
static void save() {
  Slot::write(s_state);
}
#else
static RTC_NOINIT_ATTR State s_state;   // Garbage after power-on, the magic check catches it

static void load() {}
static void save() {}
#endif

static uint32_t s_retry_ms = 0;
static bool     s_entered = false;
static bool     s_portal = false;

void onBoot() {
  load();
  bool was_safe = s_state.safe;
  Ending end = ending(DeviceInfo::resetReason());
  // An esp8266 abort() looks like a restart, so go by the dump. A brownout that left a dump stays a brownout
  if (CrashDump::reset().crash && end != END_BROWNOUT) end = END_CRASH;
  apply(s_state, MAGIC, DeviceInfo::fwFingerprint(), end);
  s_portal = s_state.portal;   // this boot only
  s_state.portal = 0;
  if (s_state.safe) {
    s_entered = !was_safe;
    // Counts the times safe mode came back, not each safe boot
    if (s_entered && s_state.retries < 255) s_state.retries++;
    s_retry_ms = retryMs(s_state.retries ? s_state.retries - 1 : 0);
  }
  save();
}

void scram() {
  s_state.enter = 1;
  save();
}

void requestPortal() {
  s_state.portal = 1;
  save();
}

void markLived() {
  s_state.lived = 1;
  save();
}

void markDay() {
  s_state.retries = 0;
  save();
}

bool     safeMode()    { return s_state.safe; }
bool     entered()      { return s_entered; }
uint32_t retryAfterMs()  { return s_retry_ms; }
uint8_t  crashStreak() { return s_state.crashStreak; }
bool     portalAsked() { return s_portal; }

}
}  // namespace corium
