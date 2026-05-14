/*
  CrashDump.cpp - see header.

  Copyright (C) 2026 @steadramon
*/
#include "CrashDump.h"

#ifdef ESP8266
#include <Arduino.h>
#include <user_interface.h>
extern "C" {
#include "cont.h"
}

namespace CrashDump {

static constexpr uint32_t MAGIC = 0xC0DECAFE;
// RTC user-memory offset (in 32-bit words). Owned by CrashDump - do not
// reuse for other persistent state.
static constexpr uint32_t RTC_OFFSET = 0;
static constexpr size_t   WORDS_TOTAL = sizeof(Snapshot) / 4;

// DRAM stack range (heap+stack live in 0x3FFE0000..0x40000000).
static constexpr uint32_t DRAM_LO = 0x3FFE0000u;
static constexpr uint32_t DRAM_HI = 0x40000000u;

static Snapshot s_recovered{};
static bool     s_have = false;

void begin() {
  Snapshot snap{};
  if (!ESP.rtcUserMemoryRead(RTC_OFFSET, (uint32_t*)&snap, sizeof(snap))) return;
  if (snap.magic != MAGIC) return;

  s_recovered = snap;
  s_have = true;

  // Wipe the magic so we don't re-report on the next clean boot.
  uint32_t zero = 0;
  ESP.rtcUserMemoryWrite(RTC_OFFSET, &zero, sizeof(zero));
}

bool hasCrash() { return s_have; }
const Snapshot* lastCrash() { return s_have ? &s_recovered : nullptr; }

} // namespace CrashDump

// Override the weak Arduino-core hook. Fires from the exception handler
// with the live stack window. Must be ISR-safe: no heap, no Serial, no
// libc beyond plain memory ops.
extern "C" void custom_crash_callback(struct rst_info* rst_info,
                                      uint32_t stack,
                                      uint32_t stack_end) {
  using namespace CrashDump;
  Snapshot snap{};
  snap.magic    = MAGIC;
  snap.reason   = rst_info ? rst_info->reason   : 0;
  snap.exccause = rst_info ? rst_info->exccause : 0;
  snap.epc1     = rst_info ? rst_info->epc1     : 0;
  snap.epc2     = rst_info ? rst_info->epc2     : 0;
  snap.epc3     = rst_info ? rst_info->epc3     : 0;
  snap.excvaddr = rst_info ? rst_info->excvaddr : 0;
  snap.depc     = rst_info ? rst_info->depc     : 0;
  snap.sp       = stack;

  uint32_t lo = stack;
  uint32_t hi = stack_end;
  if (lo >= DRAM_LO && lo < DRAM_HI && hi > lo && hi <= DRAM_HI) {
    size_t avail = (hi - lo) / 4;
    if (avail > STACK_WORDS) avail = STACK_WORDS;
    const uint32_t* p = (const uint32_t*)lo;
    for (size_t i = 0; i < avail; i++) snap.stack[i] = p[i];
    snap.word_count = avail;
  }

  ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&snap, sizeof(snap));
}

#else  // !ESP8266 - stub the namespace so callers compile.

namespace CrashDump {
void begin() {}
bool hasCrash() { return false; }
const Snapshot* lastCrash() { return nullptr; }
}

#endif
