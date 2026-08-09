/*
  EGTimer1Soft.h - soft PWM channels and Timer1 arming

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

// analogWrite and tone() pins, never spun for, can be a few us late
// times CCOUNT, lengths cpu cycles unless named ticks

#ifndef EG_TIMER1_SOFT_H
#define EG_TIMER1_SOFT_H

#include <stdint.h>

#define EGT1_SOFT_MAX   4
#define EGT1_MAX_TICKS  0x7FFFFFu   // 23 bit register
#define EGT1_LEAD_TICKS 160         // 2 us, the NMI must return first

struct EGT1Soft {
  uint32_t mask;     // 0 = free
  uint32_t high;
  uint32_t low;
  uint32_t next;     // CCOUNT
  uint32_t expiry;   // CCOUNT, 0 = never
  uint8_t  level;
};

enum : uint8_t { EGT1_CMD_NONE = 0, EGT1_CMD_START, EGT1_CMD_STOP };

// op written last, NMI clears it
struct EGT1Cmd {
  uint32_t mask;
  uint32_t high;
  uint32_t low;
  uint32_t run;      // 0 = until stopped
  volatile uint8_t ok;
  volatile uint8_t op;
};

// STOP leaves the pin as is
__attribute__((always_inline)) inline uint32_t egt1_apply(EGT1Soft* ch, uint8_t n,
                                                           EGT1Cmd& c, uint32_t now) {
  EGT1Soft* slot = nullptr;
  EGT1Soft* spare = nullptr;
  for (uint8_t i = 0; i < n; i++) {
    if (ch[i].mask == c.mask) slot = &ch[i];
    else if (!ch[i].mask && !spare) spare = &ch[i];
  }
  uint32_t expiry = c.run ? ((now + c.run) | 1u) : 0;
  c.ok = 1;
  if (c.op == EGT1_CMD_STOP) {
    if (slot) slot->mask = 0;
  } else if (slot) {
    slot->high = c.high;
    slot->low = c.low;
    slot->expiry = expiry;
  } else if (spare) {
    spare->mask = c.mask;
    spare->high = c.high;
    spare->low = c.low;
    spare->next = now;
    spare->expiry = expiry;
    spare->level = 0;
  } else {
    c.ok = 0;
  }
  c.op = EGT1_CMD_NONE;
  uint32_t carried = 0;
  for (uint8_t i = 0; i < n; i++) carried |= ch[i].mask;
  return carried;
}

__attribute__((always_inline)) inline bool egt1_soft_edge(EGT1Soft& c, uint32_t now,
                                                           uint32_t* set, uint32_t* clr) {
  if (!c.mask || (int32_t)(c.next - now) > 0) return false;
  if (c.level) {
    *clr |= c.mask;
    c.level = 0;
    c.next += c.low;
  } else {
    if (c.expiry && (int32_t)(now - c.expiry) >= 0) {
      *clr |= c.mask;
      c.mask = 0;
      return true;
    }
    *set |= c.mask;
    c.level = 1;
    c.next += c.high;
  }
  if ((int32_t)(now - c.next) >= 0) c.next = now + (c.level ? c.high : c.low);
  return true;
}

__attribute__((always_inline)) inline bool egt1_soft_next(const EGT1Soft* ch, uint8_t n,
                                                           uint32_t now, uint32_t* when) {
  bool any = false;
  int32_t best = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (!ch[i].mask) continue;
    int32_t d = (int32_t)(ch[i].next - now);
    if (!any || d < best) best = d;
    any = true;
  }
  if (any) *when = now + (uint32_t)best;
  return any;
}

enum : uint8_t { EGT1_STEP_EDGE = 1, EGT1_STEP_RUNS = 2, EGT1_STEP_FREED = 4 };

__attribute__((always_inline)) inline uint8_t egt1_soft_step(EGT1Soft* ch, uint8_t n,
                                                              uint32_t now, uint32_t* set,
                                                              uint32_t* clr, uint32_t* when) {
  uint8_t f = 0;
  int32_t best = 0;
  for (uint8_t i = 0; i < n; i++) {
    EGT1Soft& c = ch[i];
    if (!c.mask) continue;
    if (egt1_soft_edge(c, now, set, clr)) {
      f |= EGT1_STEP_EDGE;
      if (!c.mask) {
        f |= EGT1_STEP_FREED;
        continue;
      }
    }
    int32_t d = (int32_t)(c.next - now);
    if (!(f & EGT1_STEP_RUNS) || d < best) best = d;
    f |= EGT1_STEP_RUNS;
  }
  if (f & EGT1_STEP_RUNS) *when = now + (uint32_t)best;
  return f;
}

// 0 = nothing on
__attribute__((always_inline)) inline uint32_t egt1_arm(bool pwm_on, uint32_t pwm_at,
                                                         bool soft_on, uint32_t soft_at,
                                                         uint32_t now, uint8_t x2) {
  if (!pwm_on && !soft_on) return 0;
  int32_t d = INT32_MAX;
  if (pwm_on) d = (int32_t)(pwm_at - now);
  if (soft_on && (int32_t)(soft_at - now) < d) d = (int32_t)(soft_at - now);
  d >>= x2;
  if (d < (int32_t)EGT1_LEAD_TICKS) return EGT1_LEAD_TICKS;
  if ((uint32_t)d > EGT1_MAX_TICKS) return EGT1_MAX_TICKS;
  return (uint32_t)d;
}

#endif
