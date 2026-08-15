/*
  HangWatch.h - what survives a hardware watchdog

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
#ifndef HANGWATCH_H
#define HANGWATCH_H

#include <stdint.h>

// A hardware watchdog resets the chip with no exception frame, so
// custom_crash_callback never runs and CrashDump gets nothing. Only the reason
// byte survives. This writes a breadcrumb to RTC memory every second, so the
// stored copy is from under a second before whatever stalled.
//
// A software watchdog does not need this: it runs through the exception path
// and CrashDump already captures a stack.
namespace HangWatch {

struct Crumb {
  uint32_t uptime_s  = 0;
  uint32_t heap      = 0;
  uint32_t isr_calls = 0;   // serial ISR entries, 0 on builds without one
  uint32_t isr_max   = 0;   // worst single ISR, cycles
  uint32_t muted     = 0;   // times the admission budget masked the pin
  uint32_t coalesced = 0;
};

#ifdef ESP8266

// Latches the stored crumb when the last reset was a hardware watchdog, then
// clears it so a later clean boot does not re-report.
void begin();
bool had_hang();
const Crumb& last();

// The serial backend pushes its counters; left at zero otherwise.
void set_serial(uint32_t isr_calls, uint32_t isr_max, uint32_t muted, uint32_t coalesced);

// Call at 1 Hz. Stamps uptime and heap, then writes RTC.
void tick(uint32_t uptime_s);

#else

inline void begin() {}
inline bool had_hang() { return false; }
inline const Crumb& last() { static const Crumb c; return c; }
inline void set_serial(uint32_t, uint32_t, uint32_t, uint32_t) {}
inline void tick(uint32_t) {}

#endif

}

#endif
