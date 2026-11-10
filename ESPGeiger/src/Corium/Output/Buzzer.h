/*
  Buzzer.h - a buzzer on one pin, every edge timed by the RMT.

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

#ifndef CORIUM_BUZZER_H
#define CORIUM_BUZZER_H

#include <stdint.h>
#include "App/AppConfig.h"
#include "Corium/Util/Attr.h"
#ifdef ESP32
#include <soc/soc_caps.h>
#endif

// rmt loop count: c3, s3
#if defined(BUZZER_ENGINE) && defined(ESP32) && defined(SOC_RMT_SUPPORT_TX_LOOP_COUNT)
#define BUZZER_BUILT 1
#else
#define BUZZER_BUILT 0
#endif

namespace corium {
// idle is low, a held high is a fault
namespace Buzzer {

// rmt: 1 us ticks, 15 bit halves
static constexpr uint16_t MAX_TICKS  = 32767;
static constexpr uint16_t LOW_HZ     = 1000000UL / MAX_TICKS + 1;
static constexpr uint16_t MAX_CYCLES = 1023;   // c3 loop count
static constexpr uint8_t  MAX_BURST  = 8;
static constexpr uint8_t  GUARD_MS   = 20;
static constexpr uint8_t  GUARD_POLL_MS = 5;

// the app's call, set once at begin
struct Limits {
  uint16_t maxHighUs;
  uint8_t  dutyPct;   // tones and bursts
  uint8_t  gapX;      // pulses, bursts: quiet x times the on time
  uint16_t minHz;
  uint16_t maxHz;
  uint16_t runMs;     // longest unbroken tone, 0 = off
  uint16_t breakMs;   // a quiet this long ends a run
  uint16_t restMs;    // forced quiet after a cut
};
static constexpr Limits DEFAULTS = {10000, 25, 10, 200, 8000, 2000, 20, 2000};

// what the rmt can do
inline Limits fit(Limits l) {
  if (l.maxHighUs > MAX_TICKS) l.maxHighUs = MAX_TICKS;
  if (l.minHz < LOW_HZ) l.minHz = LOW_HZ;
  if (l.maxHz < l.minHz) l.maxHz = l.minHz;
  return l;
}

// high part of one cycle at hz, 1 .. max_us, always ends low
inline uint32_t highUs(uint16_t hz, uint8_t duty_pct, uint16_t max_us) {
  uint32_t period = 1000000UL / hz;
  uint32_t h = period * duty_pct / 100;
  if (h > max_us) h = max_us;
  if (h >= period) h = period - 1;
  return h ? h : 1;
}

inline uint32_t gapUs(uint32_t on_us, uint8_t x) { return on_us * x; }

inline uint16_t clampHz(uint16_t hz, const Limits& l) {
  return hz < l.minHz ? l.minHz : hz > l.maxHz ? l.maxHz : hz;
}

// cycles in ms at hz, capped before it's 16 bit
inline uint16_t toneCycles(uint16_t ms, uint16_t hz) {
  uint32_t n = (uint32_t)ms * hz / 1000;
  return n > MAX_CYCLES ? MAX_CYCLES : (uint16_t)n;
}

inline uint8_t capDuty(uint8_t duty_pct, uint8_t max_pct) {
  return duty_pct > max_pct ? max_pct : duty_pct;
}

// cycles that still fit the run, 0 = rest first
inline uint16_t runCycles(uint16_t cycles, uint32_t period_us, uint32_t used_us, uint32_t run_us) {
  if (!run_us) return cycles;
  if (used_us >= run_us) return 0;
  uint32_t n = (run_us - used_us) / period_us;
  return n < cycles ? n : cycles;
}

// tone runs: cut at runMs, restMs quiet after a cut
struct Run {
  uint32_t start = 0;
  uint32_t restAt = 0;
  bool on = false;
  bool resting = false;

  // cycles allowed for a tone at now, end = when the last sound stopped
  uint16_t take(uint32_t now, uint32_t end, uint16_t cycles, uint32_t period_us, const Limits& l) {
    if (resting) {
      if (now - restAt < l.restMs * 1000UL) return 0;
      resting = false;
      on = false;
    }
    if (!on || now - end >= l.breakMs * 1000UL) {
      start = now;
      on = true;
    }
    uint16_t n = runCycles(cycles, period_us, now - start, l.runMs * 1000UL);
    if (n < cycles) {
      resting = true;
      restAt = now + n * period_us;
    }
    return n;
  }
};

#if BUZZER_BUILT
// own timer watches the pad: quiet but still high = stop, pin low, till restart
CORIUM_NODISCARD bool begin(int pin, Limits lim = DEFAULTS);
// false while busy, faulted, or too soon for the budget
bool pulse(uint32_t now_us, uint16_t us);
bool burst(uint32_t now_us, uint16_t hz, uint8_t cycles);
bool tone(uint32_t now_us, uint16_t hz, uint16_t cycles, uint8_t duty_pct);
uint32_t waitUs(uint32_t now_us);   // 0 = free
bool faulted();
#endif

}
}  // namespace corium

#endif
