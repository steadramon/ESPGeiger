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
#include <new>
#include "RtcMap.h"
#include "DeviceInfo.h"
#include "AppHooks.h"
#include "Corium/Module/Registry.h"
#include <stdint.h>
#include <stddef.h>
#include "Corium/Platform/Flash.h"
#include "Corium/Util/Fnv.h"
#include <user_interface.h>
extern "C" {
#include "cont.h"
extern void* umm_last_fail_alloc_addr;   // the core's, a failed alloc's caller
}

namespace corium {
namespace CrashDump {

static constexpr uint32_t MAGIC = 0xC0DECB01u;
static constexpr uint32_t SNAP_WORDS = sizeof(Snapshot) / 4;
typedef RtcSlot<Snapshot, RtcMap::CRASHDUMP_AT, SNAP_WORDS> Slot;

// boots counted, a dump is only the last one's
struct Boots {
  uint32_t magic;
  uint32_t seq;
};
static constexpr uint32_t BOOTS_MAGIC = 0xB0075EC1u;
typedef RtcSlot<Boots, RtcMap::CRASHDUMP_AT + SNAP_WORDS, RtcMap::CRASHDUMP_WORDS - SNAP_WORDS> BootSlot;

// the core's user exceptions
static constexpr uint32_t USER_SMASH = 253;
static constexpr uint32_t USER_ABORT = 254;

// DRAM
static constexpr uint32_t DRAM_LO = 0x3FFE0000u;
static constexpr uint32_t DRAM_HI = 0x40000000u;

static Snapshot* s_recovered = nullptr;   // crash boots only
static uint32_t  s_fw_fp = 0;
static uint32_t  s_seq = 0;

// stack goes off-device, keep code addresses only
static bool code_word(uint32_t w) {
  return (w >= 0x40200000u && w < 0x40600000u)    // flash
      || (w >= 0x40100000u && w < 0x40108000u)    // IRAM
      || (w >= 0x40000000u && w < 0x40010000u);   // ROM
}

static uint32_t check(const Snapshot& s) {
  const uint8_t* p = (const uint8_t*)&s;
  uint32_t h = FNV_BASIS;
  for (size_t i = 0; i < offsetof(Snapshot, check); i++) h = (h ^ p[i]) * FNV_PRIME;
  return h;
}

static uint8_t dump_kind(const Snapshot& s, uint8_t chip) {
  switch (s.reason) {
    case REASON_EXCEPTION_RST: return K_EXCEPTION;
    case REASON_SOFT_WDT_RST:  return K_SW_WDT;
    case USER_ABORT:           return s.alloc ? K_OOM : K_ABORT;
    case USER_SMASH:           return K_SMASH;
    default:                   return chip >= K_BROWNOUT ? chip : K_CRASH;   // the sdk restarting itself
  }
}

static void recover(Snapshot& snap, Reset& r) {
  // a bad RTC word never reads past stack[]
  if (snap.wordCount > STACK_WORDS) snap.wordCount = STACK_WORDS;
  if (!s_recovered) s_recovered = new (std::nothrow) Snapshot(snap);
  r.kind = dump_kind(snap, r.kind);
  r.pc = r.kind == K_OOM ? snap.alloc : snap.epc1;
  r.seq = snap.seq;
}

void begin() {
  s_fw_fp = DeviceInfo::fwFingerprint();
  Boots b{};
  uint32_t last = BootSlot::read(b) && b.magic == BOOTS_MAGIC ? b.seq : 0;
  s_seq = last + 1;
  b = {BOOTS_MAGIC, s_seq};
  BootSlot::write(b);

  Reset& r = fresh(DeviceInfo::resetKind());
  Snapshot snap{};
  if (Slot::read(snap) && snap.magic == MAGIC) {
    Slot::clear();
    if (snap.check != check(snap)) r.damaged = true;
    else if (snap.fwFp == s_fw_fp && snap.seq == last) recover(snap, r);
  }
  if (r.damaged && r.kind == K_RESTART) r.kind = K_CRASH;
  settle();
}

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
  if (snap.reason == USER_ABORT) snap.alloc = (uint32_t)(uintptr_t)umm_last_fail_alloc_addr;
  snap.seq      = s_seq;   // 0 = crashed before begin()

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
  snap.check = check(snap);

  Slot::write(snap);   // check last, a torn write fails it
}

#endif
