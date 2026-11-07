/*
  CrashDump.h - Capture exception register snapshot + stack window to RTC

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
#ifndef CORIUM_CRASHDUMP_H
#define CORIUM_CRASHDUMP_H

#include <stdint.h>
#include <stddef.h>

namespace corium {
namespace CrashDump {

static constexpr size_t STACK_WORDS = 32;

// 8266: RTC memory over the reboot, esp32: from the IDF core dump summary
struct Snapshot {
  uint32_t magic;
  uint32_t fwFp;   // other firmware's dump is dropped
  uint32_t reason;
  uint32_t exccause;
  uint32_t epc1;
  uint32_t epc2;
  uint32_t epc3;
  uint32_t excvaddr;
  uint32_t depc;
  uint32_t sp;
  uint32_t wordCount;
  uint32_t stack[STACK_WORDS];   // 8266 stack code words (rest 0), esp32 backtrace PCs
};

void begin();   // early in setup()
bool hasCrash();   // this boot follows a crash of this firmware
const Snapshot* lastCrash();   // nullptr = none

} // namespace CrashDump
}  // namespace corium

#endif
