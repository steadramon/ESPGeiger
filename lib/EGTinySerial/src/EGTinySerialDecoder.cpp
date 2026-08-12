/*
  EGTinySerialDecoder.cpp - 8N1 line decoder.

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

#include "EGTinySerialDecoder.h"

namespace EGTinySerial {

uint32_t cyclesPerBit(uint32_t baud, uint32_t cpuHz) {
  if (baud == 0 || cpuHz == 0) return 0;
  const uint64_t cpb = ((uint64_t)cpuHz + (uint64_t)(baud / 2)) / (uint64_t)baud;
  if (cpb < MIN_CYCLES_PER_BIT) return 0;
  // idleCycles is cpb * MAX_RUN and must stay in 32 bits.
  if (cpb > (uint64_t)0xFFFFFFFFu / Decoder::MAX_RUN) return 0;
  return (uint32_t)cpb;
}

void Decoder::configure(uint32_t cyclesPerBit) {
  m_cyclesPerBit = cyclesPerBit;
  m_halfBit      = cyclesPerBit >> 1;
  m_idleCycles   = cyclesPerBit * MAX_RUN;
  m_bitPos       = -1;
  m_cur          = 0;
  m_resync       = false;
}

void Decoder::reset(uint32_t tick, bool level) {
  m_lastTick  = tick;
  m_lastLevel = level;
  m_bitPos    = -1;
  m_cur       = 0;
  m_resync    = false;
}

EGTS_ISR_ATTR uint8_t Decoder::bitsIn(uint32_t delta) const {
  if (delta >= m_idleCycles) return MAX_RUN;
  uint32_t rem = delta + m_halfBit;
  uint8_t  n   = 0;
  // No divide instruction on the LX106 and this runs in the interrupt
  // handler. The clamp above bounds it at MAX_RUN subtractions.
  while (rem >= m_cyclesPerBit) {
    rem -= m_cyclesPerBit;
    if (++n >= MAX_RUN) break;
  }
  return n;
}

EGTS_ISR_ATTR Emit Decoder::runOfBits(uint8_t bits, bool level) {
  Emit e;
  while (bits > 0) {
    if (m_bitPos < 0) {
      // Awaiting a start bit. A high run is idle however long it is.
      if (level) return e;
      m_bitPos = 0;
      m_cur    = 0;
      bits--;                                  // the start bit
      continue;
    }
    if (m_bitPos < 8) {
      const uint8_t room = (uint8_t)(8 - m_bitPos);
      const uint8_t take = (bits < room) ? bits : room;
      // LSB first: shift the byte down and fill from the top.
      m_cur = (uint8_t)(m_cur >> take);
      if (level) m_cur |= (uint8_t)(0xFFu << (8 - take));
      m_bitPos = (int8_t)(m_bitPos + take);
      bits = (uint8_t)(bits - take);
      continue;
    }
    // All eight data bits are in, so this bit is the stop bit.
    if (!level) {
      // Framing error. Discard the byte and the rest of the run; the next
      // transition starts a fresh frame.
      e.flags |= Emit::FRAMING;
      m_bitPos  = -1;
      return e;
    }
    e.byte   = m_cur;
    e.flags |= Emit::VALID;
    m_bitPos = -1;
    // Whatever is left of a high run is idle and cannot start a frame.
    return e;
  }
  return e;
}

EGTS_ISR_ATTR Emit Decoder::edge(uint32_t tick, bool level) {
  Emit e;
  if (m_cyclesPerBit == 0) return e;

  if (m_resync) {
    // Hold off until the line comes back up, so the tail of a break is not
    // mistaken for a start bit.
    if (level) m_resync = false;
    m_lastTick  = tick;
    m_lastLevel = level;
    return e;
  }

  if (level == m_lastLevel) {
    // GPIO status latches per pin, so two transitions arriving before the
    // handler reads the line are delivered as one. The level cannot be
    // unchanged across a genuine transition, which makes a missed pair
    // detectable here rather than a silently wrong byte later.
    e.flags    |= Emit::COALESCED;
    m_lastTick  = tick;
    m_bitPos    = -1;
    return e;
  }

  const uint8_t bits = bitsIn(tick - m_lastTick);
  const bool    held = m_lastLevel;
  m_lastTick  = tick;
  m_lastLevel = level;
  return runOfBits(bits, held);
}

Emit Decoder::idle(uint32_t tick, bool level) {
  Emit e;
  if (m_cyclesPerBit == 0) return e;

  if (m_resync) {
    // Waiting out a break. Adopt the line here once it is back up rather than
    // needing an edge, which may itself have been the one that was missed.
    if (level) {
      m_resync    = false;
      m_lastLevel = true;
      m_lastTick  = tick;
    }
    return e;
  }

  // Idle high with nothing part built. The common case, and the only one that
  // has to stay cheap: this runs on every poll.
  if (m_lastLevel && m_bitPos < 0) return e;

  if (!m_lastLevel) {
    // A low run is only consumed on the rising edge that ends it, so the
    // decoder is never mid frame while the line is down. Break detection has
    // to key off the level, not off m_bitPos.
    //
    // One bit of slack past the longest legitimate low run, which is the nine
    // bits of a 0x00 frame.
    if ((uint32_t)(tick - m_lastTick) < m_idleCycles + m_cyclesPerBit) return e;
    if (level) {
      // The line came back up without an edge being reported.
      e.flags    |= Emit::COALESCED;
      m_lastLevel = true;
    } else {
      e.flags |= Emit::BREAK;
      m_resync = true;
    }
    m_bitPos   = -1;
    m_cur      = 0;
    m_lastTick = tick;
    return e;
  }

  if ((uint32_t)(tick - m_lastTick) < m_idleCycles) return e;

  // A start bit is arriving. Leave it to the handler, which has the timing.
  if (!level) return e;

  m_lastTick = tick;
  return runOfBits(MAX_RUN, true);
}

} // namespace EGTinySerial
