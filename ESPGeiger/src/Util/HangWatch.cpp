/*
  HangWatch.cpp - what survives a hardware watchdog

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
#ifdef ESP8266

#include <Arduino.h>
#include <user_interface.h>

#include "HangWatch.h"

namespace HangWatch {

static constexpr uint32_t MAGIC = 0x48414E47u;   // 'HANG'

// CrashDump owns words 0-42, BootGuard 43-44. Do not overlap either: BootGuard
// reaching into CrashDump's window cost two words of a production stack.
// Stored is 7 words, so this claims 45-51 of the 128 available.
static constexpr uint32_t RTC_OFFSET = 45;

struct Stored {
  uint32_t magic;
  Crumb    c;
};

static Crumb s_live;
static Crumb s_last;
static bool  s_had = false;

void begin() {
  Stored st;
  if (!ESP.rtcUserMemoryRead(RTC_OFFSET, (uint32_t*)&st, sizeof(st)))
    return;

  const rst_info* ri = ESP.getResetInfoPtr();
  if (ri && ri->reason == REASON_WDT_RST && st.magic == MAGIC) {
    s_last = st.c;
    s_had  = true;
  }

  Stored zero = {};
  ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&zero, sizeof(zero));
}

bool had_hang() {
  return s_had;
}

const Crumb& last() {
  return s_last;
}

void set_serial(uint32_t isr_calls, uint32_t isr_max, uint32_t muted, uint32_t coalesced) {
  s_live.isr_calls = isr_calls;
  s_live.isr_max   = isr_max;
  s_live.muted     = muted;
  s_live.coalesced = coalesced;
}

void tick(uint32_t uptime_s) {
  s_live.uptime_s = uptime_s;
  s_live.heap     = ESP.getFreeHeap();

  Stored st;
  st.magic = MAGIC;
  st.c     = s_live;
  ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&st, sizeof(st));
}

}

#endif
