/*
  BootGuardState.h - what each boot means for the crash count and safe mode.

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

#ifndef ESPG_BOOTGUARDSTATE_H
#define ESPG_BOOTGUARDSTATE_H

#include <stdint.h>

namespace BootGuard {

enum Ending : uint8_t {
  END_OTHER,      // power-on, deep sleep
  END_RESTART,    // restart or reset pin
  END_CRASH,      // exception, watchdog or brownout
};

constexpr uint8_t CRASHES_TO_SAFE_MODE = 3;

struct State {
  uint32_t magic;
  uint32_t fw;
  uint8_t  lived;         // this boot passed LIVED_S
  uint8_t  crash_streak;  // crashes before LIVED_S
  uint8_t  safe;
  uint8_t  retries;       // safe boots since a day up
  uint8_t  enter;
  uint8_t  pad[3];
};
static_assert(sizeof(State) == 16, "State is kept in 4 RTC words");

Ending   ending(uint8_t reset_reason);
uint32_t retry_ms(uint8_t retries);      // 30 min doubling, max 16 h
void     apply(State& s, uint32_t magic, uint32_t fw, Ending end);

}

#endif
