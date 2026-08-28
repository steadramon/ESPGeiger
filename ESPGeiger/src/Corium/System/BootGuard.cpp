/*
  BootGuard.cpp - see header.

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
#include <Arduino.h>

#ifdef ESP8266
#include "RtcMap.h"
#else
#include <esp_attr.h>
#endif

namespace BootGuard {

static constexpr uint32_t MAGIC = 0xE56B0014;

#ifdef ESP8266
typedef RtcSlot<State, RtcMap::BOOTGUARD_AT, RtcMap::BOOTGUARD_WORDS> Slot;
static State s;

static void load() {
  if (!Slot::read(s)) s.magic = 0;
}
static void save() {
  Slot::write(s);
}
#else
static RTC_NOINIT_ATTR State s;   // garbage after power-on, magic catches it

static void load() {}
static void save() {}
#endif

static uint32_t s_retry_ms = 0;
static bool     s_entered = false;

void on_boot() {
  load();
  bool was_safe = s.safe;
  Ending end = ending(DeviceInfo::resetReason());
  if (CrashDump::hasCrash()) end = END_CRASH;   // esp8266 abort() looks like a restart
  apply(s, MAGIC, DeviceInfo::fwFingerprint(), end);
  if (s.safe) {
    s_entered = !was_safe;
    s_retry_ms = retry_ms(s.retries);
    if (s.retries < 255) s.retries++;
  }
  save();
}

void request_safe() {
  s.enter = 1;
  save();
}

void mark_lived() {
  s.lived = 1;
  save();
}

void mark_day() {
  s.retries = 0;
  save();
}

bool     safe_mode()    { return s.safe; }
bool     entered()      { return s_entered; }
uint32_t retry_after()  { return s_retry_ms; }
uint8_t  crash_streak() { return s.crash_streak; }

}
