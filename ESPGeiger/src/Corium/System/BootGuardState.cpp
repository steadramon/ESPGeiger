/*
  BootGuardState.cpp - crash count per boot

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

namespace corium {
namespace BootGuard {

Ending ending(uint8_t reset_reason) {
  switch (reset_reason) {
    case DeviceInfo::RR_PIN:
    case DeviceInfo::RR_RESTART:   return END_RESTART;
    case DeviceInfo::RR_EXCEPTION:
    case DeviceInfo::RR_WDT:       return END_CRASH;
    case DeviceInfo::RR_BROWNOUT:  return END_BROWNOUT;
    default:                       return END_OTHER;
  }
}

static constexpr uint8_t RETRY_DOUBLINGS = 5;
static_assert((uint64_t)CORIUM_BOOTGUARD_RETRY_MS << RETRY_DOUBLINGS <= INT32_MAX, "retry doubling runs past reached()");

uint32_t retryMs(uint8_t retries) {
  return CORIUM_BOOTGUARD_RETRY_MS << (retries < RETRY_DOUBLINGS ? retries : RETRY_DOUBLINGS);
}

void apply(State& s, uint32_t magic, uint32_t fw, Ending end) {
  // New firmware starts clean
  if (s.magic != magic || s.fw != fw) {
    memset(&s, 0, sizeof(s));
    s.magic = magic;
    s.fw = fw;
  }
  if (end == END_BROWNOUT) return;
  bool lived = s.lived;
  s.lived = 0;
  bool enter = s.enter;
  s.enter = 0;

  if (end != END_CRASH) {
    // Leaves safe mode on probation, one early crash puts it back
    s.crashStreak = s.safe ? CRASHES_TO_SAFE_MODE - 1 : 0;
    s.safe = enter && end == END_RESTART;
    return;
  }
  // a crash after LIVED_S doesn't count
  if (lived) s.crashStreak = 0;
  else if (s.crashStreak < 255) s.crashStreak++;
  if (s.crashStreak >= CRASHES_TO_SAFE_MODE || enter) s.safe = 1;
}

}
}  // namespace corium
