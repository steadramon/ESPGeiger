/*
  SendSlot.cpp - see header.

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
#include "SendSlot.h"
#include "Corium/System/Clock.h"
#include "Corium/System/DeviceInfo.h"
#include "Corium/Util/Fnv.h"

// two halves, no uint64 shift helper
static uint32_t s_claimed_lo = 0;
static uint32_t s_claimed_hi = 0;

static inline bool slot_taken(uint8_t s) {
  return s < 32 ? ((s_claimed_lo >> s) & 1u) != 0
                : ((s_claimed_hi >> (s - 32)) & 1u) != 0;
}
static inline void slot_claim(uint8_t s) {
  if (s < 32) s_claimed_lo |= (1u << s);
  else        s_claimed_hi |= (1u << (s - 32));
}

// 60 / gcd(step, 60)
static uint8_t orbit_length(uint8_t step60) {
  if (step60 == 0) return 1;
  uint8_t a = step60, b = 60;
  while (b) { uint8_t t = a % b; a = b; b = t; }
  return 60 / a;
}

// claims = others count too, else only the app's seconds
static bool orbit_clashes(uint8_t sec, uint8_t step60, uint8_t orbit_n, bool claims) {
  uint8_t s = sec; if (s >= 60) s -= 60;
  for (uint8_t k = 0; k < orbit_n; k++) {
    if (s < CORIUM_SENDSLOT_FIRST || (claims && slot_taken(s))) return true;
    s += step60; if (s >= 60) s -= 60;
  }
  return false;
}

// every second of the minute meets all the others anyway, so it only takes its own
static void orbit_claim(uint8_t sec, uint8_t step60, uint8_t orbit_n) {
  if (orbit_n == 60) orbit_n = 1;
  uint8_t s = sec; if (s >= 60) s -= 60;
  for (uint8_t k = 0; k < orbit_n; k++) {
    slot_claim(s);
    s += step60; if (s >= 60) s -= 60;
  }
}

// from start round to it again, the first sec whose orbit is clear, 0xFF = none
static uint8_t first_clear(uint8_t start, uint8_t avail, uint8_t step60, uint8_t orbit_n, bool claims) {
  uint8_t sec = start;
  do {
    if (!orbit_clashes(sec, step60, orbit_n, claims)) return sec;
    sec++; if (sec > avail) sec = CORIUM_SENDSLOT_FIRST;
  } while (sec != start);
  return 0xFF;
}

uint32_t corium::SendSlot::initialOffset(const char* mod_name, uint32_t interval_ms) {
  if (interval_ms == 0 || !mod_name) return 0;

  const uint32_t chip = corium::DeviceInfo::chipHash();
  const uint16_t jitter = (uint16_t)((chip >> 8) % 1000u);
  uint32_t h = fnv1a_P(mod_name, chip);

  uint16_t interval_s = interval_ms < 1000 ? 0 : (uint16_t)(interval_ms / 1000);
  if (interval_s < 2) return interval_ms >> 1;

  uint8_t avail = interval_s > 63 ? 63 : (uint8_t)(interval_s - 1);
  uint8_t span = (uint8_t)(avail - CORIUM_SENDSLOT_FIRST + 1);
  uint8_t step60 = (uint8_t)(interval_s % 60);
  uint8_t orbit_n = orbit_length(step60);

  uint8_t start = (uint8_t)(CORIUM_SENDSLOT_FIRST + (h % span));
  // free, else clear of the app's seconds, else the hashed one
  uint8_t sec = first_clear(start, avail, step60, orbit_n, true);
  if (sec == 0xFF) sec = first_clear(start, avail, step60, orbit_n, false);
  if (sec == 0xFF) sec = start;
  orbit_claim(sec, step60, orbit_n);
  uint32_t off = (uint32_t)sec * 1000UL + jitter;
  if (off >= interval_ms) off = (uint32_t)sec * 1000UL;
  return off;
}

unsigned long corium::SendSlot::pingFromOffset(uint32_t offset, unsigned long now, uint32_t interval_ms) {
  if (interval_ms == 0) return now;
  uint32_t until = offset;
  uint32_t secs = interval_ms / 1000;
  if (secs > 0 && corium::Clock::synced) until = untilPhase(offset, corium::Clock::phase(secs) * 1000UL, interval_ms);
  return now + until - interval_ms;
}

unsigned long corium::SendSlot::initialPing(const char* mod_name, unsigned long now, uint32_t interval_ms) {
  if (interval_ms == 0) return now;
  return pingFromOffset(initialOffset(mod_name, interval_ms), now, interval_ms);
}
