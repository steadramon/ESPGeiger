/*
  CrashDump.h - the last reset, and a crash's registers + stack window

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

// What ended the last run, K_BROWNOUT and up count as crashes. Sent as rk in the handshake, never renumber
enum Kind : uint8_t {
  K_UNKNOWN, K_POWER_ON, K_PIN, K_RESTART, K_DEEP_SLEEP,
  K_BROWNOUT, K_EXCEPTION, K_HW_WDT, K_SW_WDT, K_INT_WDT, K_TASK_WDT, K_WDT,
  K_ABORT, K_OOM, K_SMASH,
  K_CRASH,   // a damaged or unknown dump on a restart
};

// On the 8266 kept in RTC memory over the reboot, on esp32 taken from the IDF core dump summary
struct Snapshot {
  uint32_t magic;
  uint32_t fwFp;   // a dump from other firmware is dropped
  uint32_t reason;
  uint32_t exccause;
  uint32_t epc1;
  uint32_t epc2;
  uint32_t epc3;
  uint32_t excvaddr;
  uint32_t depc;
  uint32_t sp;
  uint32_t wordCount;
  uint32_t in;   // 8266, the Registry slot the loop was in
  uint32_t stack[STACK_WORDS];   // 8266 stack code words (rest 0), esp32 backtrace PCs
  uint32_t alloc;   // 8266 abort, the last failed alloc's caller
  uint32_t seq;     // 8266, the boot it was written in
  uint32_t check;   // 8266, written last so a torn write fails it
};

// The last reset, read once by begin()
struct Reset {
  uint8_t  kind;
  bool     crash;     // a crash kind, or a damaged dump
  bool     damaged;   // a dump that fails its check
  uint32_t pc;        // pc from the dump, on OOM the failed alloc's caller. 0 = none
  uint32_t seq;       // 8266, boot the dump was written in
};

void begin();   // early in setup()
const Snapshot* lastCrash();   // nullptr = none
const Reset& reset();
const char* kindText(uint8_t kind);   // PROGMEM

// For the chip files' begin()
Reset& fresh(uint8_t kind);
void settle();

} // namespace CrashDump
}  // namespace corium

#endif
