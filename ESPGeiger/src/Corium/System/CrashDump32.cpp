/*
  CrashDump32.cpp - crash snapshot, the esp32's from the IDF coredump

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
#ifdef ESP32
#include "CrashDump.h"
#include "DeviceInfo.h"
#include <new>
// IDF coredump partition
#include <esp_core_dump.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <string.h>

namespace corium {
namespace CrashDump {

static Snapshot* s_recovered = nullptr;   // crash boots only

// Resets the panic handler may have written a dump for
static bool panic_reset(uint8_t kind) {
  return kind == K_EXCEPTION || kind == K_INT_WDT || kind == K_TASK_WDT || kind == K_WDT;
}

static void from_summary(Snapshot& snap, const esp_core_dump_summary_t& sum) {
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
  // RISC-V gives raw stack bytes, not PCs
  snap.epc1     = sum.exc_pc;
  snap.exccause = sum.ex_info.mcause;
  snap.excvaddr = sum.ex_info.mtval;
  snap.sp       = sum.ex_info.sp;
  size_t bytes  = sum.exc_bt_info.dump_size;
  if (bytes > sizeof(snap.stack)) bytes = sizeof(snap.stack);
  memcpy(snap.stack, sum.exc_bt_info.stackdump, bytes);
  snap.wordCount = (uint32_t)(bytes / sizeof(uint32_t));
#else
  snap.exccause = sum.ex_info.exc_cause;
  snap.epc1     = sum.exc_pc;
  // epcx[n] is only valid when its bit is set
  snap.epc2     = (sum.ex_info.epcx_reg_bits & (1u << 1)) ? sum.ex_info.epcx[1] : 0;
  snap.epc3     = (sum.ex_info.epcx_reg_bits & (1u << 2)) ? sum.ex_info.epcx[2] : 0;
  snap.excvaddr = sum.ex_info.exc_vaddr;
  snap.depc     = 0;
  snap.sp       = sum.ex_info.exc_a[1];
  size_t depth = sum.exc_bt_info.depth;
  if (depth > STACK_WORDS) depth = STACK_WORDS;
  for (size_t i = 0; i < depth; i++) snap.stack[i] = sum.exc_bt_info.bt[i];
  snap.wordCount = (uint32_t)depth;
#endif
}

static void read(Reset& r) {
  if (esp_core_dump_image_check() != ESP_OK) {
    r.damaged = true;
    return;
  }
  esp_core_dump_summary_t sum{};
  if (esp_core_dump_get_summary(&sum) != ESP_OK) return;
  // Dump from old firmware, wrong ELF
  char sha[APP_ELF_SHA256_SZ];
  esp_ota_get_app_elf_sha256(sha, sizeof(sha));
  if (strncmp((const char*)sum.app_elf_sha256, sha, sizeof(sha)) != 0) return;
  Snapshot snap{};
  snap.reason = (uint32_t)esp_reset_reason();
  from_summary(snap, sum);
  if (!s_recovered) s_recovered = new (std::nothrow) Snapshot(snap);
  r.pc = snap.epc1;
}

void begin() {
  Reset& r = fresh(DeviceInfo::resetKind());
  size_t at, size;
  if (esp_core_dump_image_get(&at, &size) == ESP_OK) {
    if (panic_reset(r.kind)) read(r);
    // Erase it, if left a later crash would show it as its own
    esp_core_dump_image_erase();
  }
  settle();
}

const Snapshot* lastCrash() { return s_recovered; }

} // namespace CrashDump
}  // namespace corium

#endif
