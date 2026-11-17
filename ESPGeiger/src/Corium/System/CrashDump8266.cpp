/*
  CrashDump8266.cpp - crash snapshot to RTC, the esp8266's

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
#include "CrashDump.h"
#include "StatusWord.h"
#include <new>
#include "RtcMap.h"
#include "DeviceInfo.h"
#include "AppHooks.h"
#include "Corium/Module/Registry.h"
#include <stdint.h>
#include <stddef.h>
#include "Corium/Platform/Flash.h"
#include <user_interface.h>
extern "C" {
#include "cont.h"
}

namespace corium {
namespace CrashDump {

static constexpr uint32_t MAGIC = 0xC0DECAFFu;
typedef RtcSlot<Snapshot, RtcMap::CRASHDUMP_AT, RtcMap::CRASHDUMP_WORDS> Slot;

// DRAM
static constexpr uint32_t DRAM_LO = 0x3FFE0000u;
static constexpr uint32_t DRAM_HI = 0x40000000u;

static Snapshot* s_recovered = nullptr;   // crash boots only
static uint32_t  s_fw_fp = 0;

// stack goes off-device, keep code addresses only
static bool code_word(uint32_t w) {
  return (w >= 0x40200000u && w < 0x40600000u)    // flash
      || (w >= 0x40100000u && w < 0x40108000u)    // IRAM
      || (w >= 0x40000000u && w < 0x40010000u);   // ROM
}

void begin() {
  s_fw_fp = DeviceInfo::fwFingerprint();
  Snapshot snap{};
  if (!Slot::read(snap)) return;
  if (snap.magic != MAGIC) return;
  Slot::clear();
  if (snap.fwFp != s_fw_fp) return;
  // a bad RTC word never reads past stack[]
  if (snap.wordCount > STACK_WORDS) snap.wordCount = STACK_WORDS;
  if (!s_recovered) s_recovered = new (std::nothrow) Snapshot(snap);
  if (s_recovered) corium::healthWord().set(corium::DEV_CRASHED, true);
}

bool hasCrash() { return s_recovered != nullptr; }
const Snapshot* lastCrash() { return s_recovered; }

} // namespace CrashDump
}  // namespace corium

// Exception handler - no heap, Serial or libc
extern "C" void custom_crash_callback(struct rst_info* rst_info,
                                      uint32_t stack,
                                      uint32_t stack_end) {
  corium::AppHooks::onCrash();   // outputs off first, the stack takes up to a second
  using namespace corium::CrashDump;
  Snapshot snap{};
  snap.magic    = MAGIC;
  snap.fwFp     = s_fw_fp;   // 0 = crashed before begin()
  if (rst_info) {
    snap.reason   = rst_info->reason;
    snap.exccause = rst_info->exccause;
    snap.epc1     = rst_info->epc1;
    snap.epc2     = rst_info->epc2;
    snap.epc3     = rst_info->epc3;
    snap.excvaddr = rst_info->excvaddr;
    snap.depc     = rst_info->depc;
  }
  snap.sp       = stack;
  snap.in       = corium::Registry::firing();

  uint32_t lo = stack;
  uint32_t hi = stack_end;
  // copy only a stack that sits in DRAM
  if (lo >= DRAM_LO && lo < DRAM_HI && hi > lo && hi <= DRAM_HI) {
    size_t avail = (hi - lo) / 4;
    if (avail > STACK_WORDS) avail = STACK_WORDS;
    const uint32_t* p = (const uint32_t*)lo;
    for (size_t i = 0; i < avail; i++) snap.stack[i] = code_word(p[i]) ? p[i] : 0;
    snap.wordCount = avail;
  }

  Slot::write(snap);
}

#endif
