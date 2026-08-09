/*
  EGTimer1Rx.h - serial receive timed by the EGTimer1 handler

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

// 8N1 rx sampled in the NMI, a spinning sw serial trips "dev 1163"
// no hw here, host tested, times CCOUNT, lengths cpu cycles

#ifndef EG_TIMER1_RX_H
#define EG_TIMER1_RX_H

#include <stdint.h>

#define EGRX_MARK 0xFF

struct EGT1Rx {
  uint32_t cpb;        // cycles per bit
  uint32_t next;       // CCOUNT, aimed early
  uint8_t  on;
  uint8_t  bitn;       // 1..8 data, 9 stop
  uint8_t  cur;
  uint8_t  first;
  uint32_t starts, resyncs, framing, framing_first, late, already_low;
};

enum : uint8_t { EGRX_NEXT, EGRX_BYTE, EGRX_FRAMING, EGRX_LATE };

// first sample 1.5 bits after the edge
__attribute__((always_inline)) inline void egrx_start(EGT1Rx& r, uint32_t entry,
                                                      uint32_t lat, uint32_t bias) {
  r.starts++;
  r.bitn = 1;
  r.cur = 0;
  r.first = 1;
  r.next = entry - lat + r.cpb + r.cpb / 2 - bias;
  // NMI may run between stores, on goes last
  __asm__ __volatile__("" ::: "memory");
  r.on = 1;
}

// over half a bit late = dropped
__attribute__((always_inline)) inline uint8_t egrx_sample(EGT1Rx& r, bool v, uint32_t late) {
  if (late > r.cpb / 2) {
    r.late++;
    r.on = 0;
    return EGRX_LATE;
  }
  if (r.bitn <= 8) {
    r.cur = (uint8_t)((r.cur >> 1) | (v ? 0x80 : 0));
    r.bitn++;
    r.next += r.cpb;
    return EGRX_NEXT;
  }
  if (!v) {
    r.framing++;
    if (r.first) r.framing_first++;
    r.on = 0;
    return EGRX_FRAMING;
  }
  return EGRX_BYTE;
}

// already low: edge is at latest the stop bit end
__attribute__((always_inline)) inline void egrx_resync(EGT1Rx& r, uint32_t t, uint32_t now,
                                                       bool waited, uint32_t bias) {
  uint32_t edge = now;
  if (!waited) {
    r.already_low++;
    uint32_t earliest = t + bias + r.cpb / 2;
    if ((int32_t)(now - earliest) > 0) edge = earliest;
  }
  r.resyncs++;
  r.bitn = 1;
  r.cur = 0;
  r.first = 0;
  r.next = edge + r.cpb + r.cpb / 2 - bias;
}

#endif
