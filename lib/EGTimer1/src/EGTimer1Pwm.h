/*
  EGTimer1Pwm.h - edge scheduling for the precise PWM channel

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

// Fixed timeline, a late edge never moves the next
// lengths are 80 MHz ticks, times CCOUNT, shift by x2

#ifndef EG_TIMER1_PWM_H
#define EG_TIMER1_PWM_H

#include <stdint.h>

#define EGT1_PWM_TICK_HZ   80000000u
#define EGT1_PWM_MIN_OFF   320u   // 4 us, never high a whole period
#define EGT1_PWM_IDLE      80000u // 1 ms

enum : uint8_t { EGT1_PWM_WAIT = 0, EGT1_PWM_RISE, EGT1_PWM_FALL, EGT1_PWM_SKIP };

struct EGT1PwmMailbox {
  volatile uint32_t period;
  volatile uint32_t on;
  volatile uint8_t  ready;
};

struct EGT1PwmState {
  uint32_t next;    // CCOUNT
  uint32_t start;   // CCOUNT
  uint32_t period;
  uint32_t on;      // 0 = held low
  uint32_t skips;
  uint32_t debt;    // cycles late, off the next pulse
  uint8_t  high;
};

// pin goes low every period, even at full duty
inline void egt1_pwm_ticks(uint32_t freq, uint32_t duty, uint32_t range,
                           uint32_t* period, uint32_t* on) {
  uint32_t p = EGT1_PWM_TICK_HZ / (freq ? freq : 1);
  uint32_t o = range ? (uint32_t)(((uint64_t)p * duty) / range) : 0;
  uint32_t cap = p > EGT1_PWM_MIN_OFF ? p - EGT1_PWM_MIN_OFF : 0;
  *period = p;
  *on = o > cap ? cap : o;
}

// clear ready first, the NMI may run mid-write
inline void egt1_pwm_post(EGT1PwmMailbox& mb, uint32_t period, uint32_t on) {
  mb.ready = 0;
  mb.period = period;
  mb.on = on;
  mb.ready = 1;
}

// runs in the NMI, keep always_inline, no flash calls
__attribute__((always_inline)) inline uint8_t egt1_pwm_step(EGT1PwmState& s, EGT1PwmMailbox& mb,
                                                           uint32_t now, uint8_t x2, uint32_t lead,
                                                           uint32_t* due) {
  if ((int32_t)(s.next - now) > (int32_t)lead) return EGT1_PWM_WAIT;
  if (!s.period && !mb.ready) {
    s.next = now + (EGT1_PWM_IDLE << x2);
    return EGT1_PWM_WAIT;
  }
  *due = s.next;
  if (s.high) {
    s.high = 0;
    s.next = s.start + (s.period << x2);
    return EGT1_PWM_FALL;
  }
  if (mb.ready) {
    s.period = mb.period;
    s.on = mb.on;
    mb.ready = 0;
  }
  s.start = s.next;
  s.next = s.start + (s.period << x2);
  uint32_t on = s.on << x2;
  if (!on) return EGT1_PWM_SKIP;
  if ((int32_t)(now - (s.start + on - lead)) >= 0) {
    s.skips++;
    return EGT1_PWM_SKIP;
  }
  s.high = 1;
  s.next = s.start + on;
  return EGT1_PWM_RISE;
}

// a late rise keeps its full on-time
__attribute__((always_inline)) inline void egt1_pwm_fall_from(EGT1PwmState& s, uint32_t rose,
                                                              uint8_t x2) {
  uint32_t fall = rose + (s.on << x2) - s.debt;
  s.debt = 0;
  uint32_t limit = s.start + ((s.period - EGT1_PWM_MIN_OFF) << x2);
  s.next = (int32_t)(fall - limit) > 0 ? limit : fall;
}

// next pulse at most a quarter shorter
__attribute__((always_inline)) inline void egt1_pwm_late(EGT1PwmState& s, uint32_t late,
                                                         uint8_t x2) {
  uint32_t cap = (s.on << x2) >> 2;
  s.debt = late > cap ? cap : late;
}

#endif
