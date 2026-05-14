/*
  CrashDump.h - Capture exception register snapshot + stack window to RTC
  user RAM so the next boot can report it. ESP8266 only for now.

  Copyright (C) 2026 @steadramon
*/
#ifndef _ESPG_CRASHDUMP_H
#define _ESPG_CRASHDUMP_H

#include <stdint.h>
#include <stddef.h>

namespace CrashDump {

static constexpr size_t STACK_WORDS = 32;   // ~128 B of stack snapshot

struct Snapshot {
  uint32_t magic;
  uint32_t reason;
  uint32_t exccause;
  uint32_t epc1;
  uint32_t epc2;
  uint32_t epc3;
  uint32_t excvaddr;
  uint32_t depc;
  uint32_t sp;
  uint32_t word_count;
  uint32_t stack[STACK_WORDS];
};

// Early in setup(): reads RTC user RAM. If a fresh snapshot is present,
// stages it for accessors and clears RTC so the next clean boot doesn't
// re-report it. Cheap when there's nothing to recover.
void begin();

// True if a valid snapshot was found at boot.
bool hasCrash();

// Pointer to the staged snapshot, or nullptr if hasCrash() is false.
const Snapshot* lastCrash();

} // namespace CrashDump

#endif
