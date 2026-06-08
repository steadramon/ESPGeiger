/*
  CrashDump.cpp - see header.

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

#elif defined(ESP32)
// ESP32 path uses the IDF coredump partition; read summary then erase.
#include <Arduino.h>
#include <esp_core_dump.h>
#include <esp_system.h>

namespace CrashDump {

static Snapshot s_recovered{};
static bool     s_have = false;

void begin() {
  // Fast path: skip unless previous boot panicked and a coredump exists.
  esp_reset_reason_t rr = esp_reset_reason();
  bool was_panic = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT ||
                    rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT);
  if (!was_panic) return;
  if (esp_core_dump_image_check() != ESP_OK) return;

  esp_core_dump_summary_t sum{};
  if (esp_core_dump_get_summary(&sum) == ESP_OK) {
    Snapshot snap{};
    snap.reason   = (uint32_t)rr;
    snap.exccause = sum.ex_info.exc_cause;
    snap.epc1     = sum.exc_pc;
    snap.epc2     = (sum.ex_info.epcx_reg_bits & (1u << 1)) ? sum.ex_info.epcx[1] : 0;
    snap.epc3     = (sum.ex_info.epcx_reg_bits & (1u << 2)) ? sum.ex_info.epcx[2] : 0;
    snap.excvaddr = sum.ex_info.exc_vaddr;
    snap.depc     = 0;
    snap.sp       = sum.ex_info.exc_a[1];

    size_t depth = sum.exc_bt_info.depth;
    if (depth > STACK_WORDS) depth = STACK_WORDS;
    for (size_t i = 0; i < depth; i++) snap.stack[i] = sum.exc_bt_info.bt[i];
    snap.word_count = (uint32_t)depth;

    s_recovered = snap;
    s_have = true;
  }

  // Always erase so a bad image doesn't trip the same parse next boot.
  esp_core_dump_image_erase();
}

bool hasCrash() { return s_have; }
const Snapshot* lastCrash() { return s_have ? &s_recovered : nullptr; }

} // namespace CrashDump

#else  // Other targets - stub.
namespace CrashDump {
void begin() {}
bool hasCrash() { return false; }
const Snapshot* lastCrash() { return nullptr; }
}
#endif
