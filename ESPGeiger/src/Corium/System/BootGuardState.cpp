/*
  BootGuardState.cpp - see header.

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
#include "BootGuardState.h"
#include "DeviceInfo.h"
#include <string.h>

namespace BootGuard {

Ending ending(uint8_t reset_reason) {
  switch (reset_reason) {
    case DeviceInfo::RR_PIN:
    case DeviceInfo::RR_RESTART:   return END_RESTART;
    case DeviceInfo::RR_EXCEPTION:
    case DeviceInfo::RR_WDT:
    case DeviceInfo::RR_BROWNOUT:  return END_CRASH;
    default:                       return END_OTHER;
  }
}

uint32_t retry_ms(uint8_t retries) {
  return (30UL * 60 * 1000) << (retries < 5 ? retries : 5);
}

void apply(State& s, uint32_t magic, uint32_t fw, Ending end) {
  if (s.magic != magic || s.fw != fw) {
    memset(&s, 0, sizeof(s));
    s.magic = magic;
    s.fw = fw;
  }
  bool lived = s.lived;
  s.lived = 0;
  bool enter = s.enter;
  s.enter = 0;

  if (end != END_CRASH) {
    s.crash_streak = 0;
    s.safe = enter && end == END_RESTART;
    return;
  }
  if (lived) s.crash_streak = 0;
  else if (s.crash_streak < 255) s.crash_streak++;
  if (s.crash_streak >= CRASHES_TO_SAFE_MODE || enter) s.safe = 1;
}

}
