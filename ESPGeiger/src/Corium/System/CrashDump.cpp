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
#include <new>

#ifdef ESP8266
#include "RtcMap.h"
#include "DeviceInfo.h"
#include <Arduino.h>
#include <user_interface.h>
extern "C" {
#include "cont.h"
}

namespace CrashDump {

static constexpr uint32_t MAGIC = 0xC0DECAFFu;
typedef RtcSlot<Snapshot, RtcMap::CRASHDUMP_AT, RtcMap::CRASHDUMP_WORDS> Slot;

// DRAM
static constexpr uint32_t DRAM_LO = 0x3FFE0000u;
static constexpr uint32_t DRAM_HI = 0x40000000u;

static Snapshot* s_recovered = nullptr;   // crash boots only
static uint32_t  s_fw_fp = 0;

void begin() {
  s_fw_fp = DeviceInfo::fwFingerprint();
  Snapshot snap{};
  if (!Slot::read(snap)) return;
  if (snap.magic != MAGIC) return;
  Slot::clear();
  if (snap.fw_fp != s_fw_fp) return;
  if (!s_recovered) s_recovered = new (std::nothrow) Snapshot(snap);
}

bool hasCrash() { return s_recovered != nullptr; }
const Snapshot* lastCrash() { return s_recovered; }

} // namespace CrashDump

// Exception handler - no heap, Serial or libc
extern "C" void custom_crash_callback(struct rst_info* rst_info,
                                      uint32_t stack,
                                      uint32_t stack_end) {
  using namespace CrashDump;
  Snapshot snap{};
  snap.magic    = MAGIC;
  snap.fw_fp    = s_fw_fp;   // 0 = crashed before begin()
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

  Slot::write(snap);
}

#elif defined(ESP32)
// IDF coredump partition
#include <esp_core_dump.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <string.h>

namespace CrashDump {

static Snapshot* s_recovered = nullptr;   // crash boots only

void begin() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool was_panic = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT ||
                    rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT);
  if (!was_panic) return;
  if (esp_core_dump_image_check() != ESP_OK) return;

  esp_core_dump_summary_t sum{};
  if (esp_core_dump_get_summary(&sum) == ESP_OK) {
    // Old firmware, wrong ELF
    char sha[APP_ELF_SHA256_SZ];
    esp_ota_get_app_elf_sha256(sha, sizeof(sha));
    if (strncmp((const char*)sum.app_elf_sha256, sha, sizeof(sha)) != 0) {
      esp_core_dump_image_erase();
      return;
    }
    Snapshot snap{};
    snap.reason = (uint32_t)rr;
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
    // RISC-V, raw stack bytes not PCs
    snap.epc1     = sum.exc_pc;
    snap.exccause = sum.ex_info.mcause;
    snap.excvaddr = sum.ex_info.mtval;
    snap.sp       = sum.ex_info.sp;
    size_t bytes  = sum.exc_bt_info.dump_size;
    if (bytes > sizeof(snap.stack)) bytes = sizeof(snap.stack);
    memcpy(snap.stack, sum.exc_bt_info.stackdump, bytes);
    snap.word_count = (uint32_t)(bytes / sizeof(uint32_t));
#else
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
#endif
    if (!s_recovered) s_recovered = new (std::nothrow) Snapshot(snap);
  }

  // always, or a bad image loops
  esp_core_dump_image_erase();
}

bool hasCrash() { return s_recovered != nullptr; }
const Snapshot* lastCrash() { return s_recovered; }

} // namespace CrashDump

#else  // Other targets - stub.
namespace CrashDump {
void begin() {}
bool hasCrash() { return false; }
const Snapshot* lastCrash() { return nullptr; }
}
#endif
